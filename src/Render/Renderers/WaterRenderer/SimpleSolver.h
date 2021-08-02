#pragma once

#pragma pack(push, 1)
struct Point
{
  glm::vec4 worldPos;
  glm::vec4 worldNormal;
  glm::vec4 directLight;
  glm::vec4 indirectLight;
  float worldRadius;
  float padding[3];
};
#pragma pack(pop)

struct VolumeProxy
{
  VolumeProxy(legit::RenderGraph *renderGraph, vk::Format format, glm::uvec3 _baseSize, vk::ImageUsageFlags usageFlags) :
    baseSize(_baseSize)
  {
    imageProxy = renderGraph->AddImage(format, 1, 1, _baseSize, usageFlags);
    imageViewProxy = renderGraph->AddImageView(imageProxy->Id(), 0, 1, 0, 1);
  }
  legit::RenderGraph::ImageProxyUnique imageProxy;
  legit::RenderGraph::ImageViewProxyUnique imageViewProxy;
  glm::uvec2 baseSize;
};

class SimpleSolver
{
public:
  SimpleSolver(legit::Core *_core)
  {
    this->core = _core;
    linearSampler.reset(new legit::Sampler(core->GetLogicalDevice(), vk::SamplerAddressMode::eClampToEdge, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear));

    ReloadShaders();
  }
  struct SolverBuffers
  {
    legit::RenderGraph::ImageViewProxyId velocityProxyId;
  };
  void RecreateSwapchainResources(glm::uvec2 viewportSize, size_t framesInFlightCount, size_t maxMipsCount = std::numeric_limits<size_t>::max())
  {
    //viewportResources.reset(new ViewportResources(core, viewportSize, maxMipsCount));
  }

  void RecreateSceneResources(glm::uvec3 volumeResolution, glm::vec3 volumeMin, glm::vec3 volumeMax)
  {
    sceneResources.reset(new SceneResources(core, volumeResolution, volumeMin, volumeMax));
  }

  SolverBuffers Update(legit::ShaderMemoryPool *memoryPool, legit::RenderGraph::BufferProxyId pointsDataProxyId, uint32_t pointsCount)
  {
    SimulationData simulationData;
    simulationData.volumeResolution = glm::uvec4(sceneResources->volumeResolution, 0.0f);
    simulationData.volumeMin = glm::vec4(sceneResources->volumeMin, 0.0f);
    simulationData.volumeMax = glm::vec4(sceneResources->volumeMax, 0.0f);
    glm::vec3 stepSize = (sceneResources->volumeMax - sceneResources->volumeMin) / glm::vec3(sceneResources->volumeResolution);
    simulationData.stepSize = glm::vec4(stepSize, 0.0f);
    simulationData.invStepSize = glm::vec4(glm::vec3(1.0f) / stepSize, 0.0f);
    simulationData.timeStep = 1e-2f;
    simulationData.particlesCount = pointsCount;
    simulationData.iterationIndex = 0;

    static bool isFirst = true;
    if(isFirst || ImGui::Button("Reset"))
    {
      core->GetRenderGraph()->AddPass(legit::RenderGraph::ComputePassDesc()
        .SetStorageImages({
          sceneResources->velocityVolumeProxy.imageViewProxy->Id(),
          sceneResources->pressureVolumeProxy.imageViewProxy->Id() })
        .SetProfilerInfo(legit::Colors::emerald, "PassFieldsInit")
        .SetRecordFunc([this, memoryPool, simulationData](legit::RenderGraph::PassContext passContext)
      {
        auto shader = fieldsInitShader.compute.get();
        auto pipeineInfo = this->core->GetPipelineCache()->BindComputePipeline(passContext.GetCommandBuffer(), shader);
        {
          const legit::DescriptorSetLayoutKey *shaderDataSetInfo = shader->GetSetInfo(ShaderDataSetIndex);
          auto shaderData = memoryPool->BeginSet(shaderDataSetInfo);
          {
            auto shaderPassDataBuffer = memoryPool->GetUniformBufferData<SimulationData>("SimulationDataBuffer");
            *shaderPassDataBuffer = simulationData;
          }
          memoryPool->EndSet();

          std::vector<legit::StorageImageBinding> storageImageBindings;
          auto velocityVolumeProxy = passContext.GetImageView(sceneResources->velocityVolumeProxy.imageViewProxy->Id());
          storageImageBindings.push_back(shaderDataSetInfo->MakeStorageImageBinding("velocityImage", velocityVolumeProxy));
          auto pressureVolumeProxy = passContext.GetImageView(sceneResources->pressureVolumeProxy.imageViewProxy->Id());
          storageImageBindings.push_back(shaderDataSetInfo->MakeStorageImageBinding("pressureImage", pressureVolumeProxy));

          auto shaderDataSetBindings = legit::DescriptorSetBindings()
            .SetUniformBufferBindings(shaderData.uniformBufferBindings)
            .SetStorageImageBindings(storageImageBindings);/*
            .SetImageSamplerBindings(imageSamplerBindings);*/

          auto shaderDataSet = this->core->GetDescriptorSetCache()->GetDescriptorSet(*shaderDataSetInfo, shaderDataSetBindings);
          passContext.GetCommandBuffer().bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipeineInfo.pipelineLayout, ShaderDataSetIndex, { shaderDataSet }, { shaderData.dynamicOffset });

          glm::uvec3 workGroupSize = shader->GetLocalSize();
          passContext.GetCommandBuffer().dispatch(
            uint32_t(sceneResources->volumeResolution.x / workGroupSize.x),
            uint32_t(sceneResources->volumeResolution.y / workGroupSize.y),
            uint32_t(sceneResources->volumeResolution.z / workGroupSize.z));
        }
      }));
    }
    isFirst = false;
   
    core->GetRenderGraph()->AddPass(legit::RenderGraph::ComputePassDesc()
      .SetStorageImages({
        sceneResources->tmpVelocityVolumeProxy.imageViewProxy->Id(),
        sceneResources->tmpPressureVolumeProxy.imageViewProxy->Id()})
      .SetInputImages({
        sceneResources->velocityVolumeProxy.imageViewProxy->Id(),
        sceneResources->pressureVolumeProxy.imageViewProxy->Id() })
      .SetProfilerInfo(legit::Colors::emerald, "PassFieldsAdvect")
      .SetRecordFunc([this, memoryPool, simulationData](legit::RenderGraph::PassContext passContext)
    {
      auto shader = fieldsAdvectShader.compute.get();
      auto pipeineInfo = this->core->GetPipelineCache()->BindComputePipeline(passContext.GetCommandBuffer(), shader);
      {
        const legit::DescriptorSetLayoutKey *shaderDataSetInfo = shader->GetSetInfo(ShaderDataSetIndex);
        auto shaderData = memoryPool->BeginSet(shaderDataSetInfo);
        {
          auto shaderPassDataBuffer = memoryPool->GetUniformBufferData<SimulationData>("SimulationDataBuffer");
          *shaderPassDataBuffer = simulationData;
        }
        memoryPool->EndSet();

        std::vector<legit::ImageSamplerBinding> imageSamplerBindings;
        auto velocityImageView = passContext.GetImageView(sceneResources->velocityVolumeProxy.imageViewProxy->Id());
        imageSamplerBindings.push_back(shaderDataSetInfo->MakeImageSamplerBinding("srcVelocitySampler", velocityImageView, linearSampler.get()));
        auto pressureImageView = passContext.GetImageView(sceneResources->pressureVolumeProxy.imageViewProxy->Id());
        imageSamplerBindings.push_back(shaderDataSetInfo->MakeImageSamplerBinding("srcPressureSampler", pressureImageView, linearSampler.get()));

        std::vector<legit::StorageImageBinding> storageImageBindings;
        auto velocityVolumeProxy = passContext.GetImageView(sceneResources->tmpVelocityVolumeProxy.imageViewProxy->Id());
        storageImageBindings.push_back(shaderDataSetInfo->MakeStorageImageBinding("dstVelocityImage", velocityVolumeProxy));
        auto pressureVolumeProxy = passContext.GetImageView(sceneResources->tmpPressureVolumeProxy.imageViewProxy->Id());
        storageImageBindings.push_back(shaderDataSetInfo->MakeStorageImageBinding("dstPressureImage", pressureVolumeProxy));

        auto shaderDataSetBindings = legit::DescriptorSetBindings()
          .SetUniformBufferBindings(shaderData.uniformBufferBindings)
          .SetStorageImageBindings(storageImageBindings)
          .SetImageSamplerBindings(imageSamplerBindings);

        auto shaderDataSet = this->core->GetDescriptorSetCache()->GetDescriptorSet(*shaderDataSetInfo, shaderDataSetBindings);
        passContext.GetCommandBuffer().bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipeineInfo.pipelineLayout, ShaderDataSetIndex, { shaderDataSet }, { shaderData.dynamicOffset });

        glm::uvec3 workGroupSize = shader->GetLocalSize();
        passContext.GetCommandBuffer().dispatch(
          uint32_t(sceneResources->volumeResolution.x / workGroupSize.x),
          uint32_t(sceneResources->volumeResolution.y / workGroupSize.y),
          uint32_t(sceneResources->volumeResolution.z / workGroupSize.z));
      }
    }));

    core->GetRenderGraph()->AddPass(legit::RenderGraph::ComputePassDesc()
      .SetStorageImages({
        sceneResources->velocityVolumeProxy.imageViewProxy->Id(),
        sceneResources->pressureVolumeProxy.imageViewProxy->Id(),
        sceneResources->tmpVelocityVolumeProxy.imageViewProxy->Id(),
        sceneResources->tmpPressureVolumeProxy.imageViewProxy->Id() })
      .SetProfilerInfo(legit::Colors::emerald, "PassFieldsCopy")
      .SetRecordFunc([this, memoryPool, simulationData](legit::RenderGraph::PassContext passContext)
    {
      auto shader = fieldsCopyShader.compute.get();
      auto pipeineInfo = this->core->GetPipelineCache()->BindComputePipeline(passContext.GetCommandBuffer(), shader);
      {
        const legit::DescriptorSetLayoutKey *shaderDataSetInfo = shader->GetSetInfo(ShaderDataSetIndex);
        auto shaderData = memoryPool->BeginSet(shaderDataSetInfo);
        {
          auto shaderPassDataBuffer = memoryPool->GetUniformBufferData<SimulationData>("SimulationDataBuffer");
          *shaderPassDataBuffer = simulationData;
        }
        memoryPool->EndSet();

        std::vector<legit::StorageImageBinding> storageImageBindings;
        auto srcVelocityVolumeProxy = passContext.GetImageView(sceneResources->tmpVelocityVolumeProxy.imageViewProxy->Id());
        storageImageBindings.push_back(shaderDataSetInfo->MakeStorageImageBinding("srcVelocityImage", srcVelocityVolumeProxy));
        auto srcPressureVolumeProxy = passContext.GetImageView(sceneResources->tmpPressureVolumeProxy.imageViewProxy->Id());
        storageImageBindings.push_back(shaderDataSetInfo->MakeStorageImageBinding("srcPressureImage", srcPressureVolumeProxy));
        auto dstVelocityVolumeProxy = passContext.GetImageView(sceneResources->velocityVolumeProxy.imageViewProxy->Id());
        storageImageBindings.push_back(shaderDataSetInfo->MakeStorageImageBinding("dstVelocityImage", dstVelocityVolumeProxy));
        auto dstPressureVolumeProxy = passContext.GetImageView(sceneResources->pressureVolumeProxy.imageViewProxy->Id());
        storageImageBindings.push_back(shaderDataSetInfo->MakeStorageImageBinding("dstPressureImage", dstPressureVolumeProxy));

        auto shaderDataSetBindings = legit::DescriptorSetBindings()
          .SetUniformBufferBindings(shaderData.uniformBufferBindings)
          .SetStorageImageBindings(storageImageBindings);

        auto shaderDataSet = this->core->GetDescriptorSetCache()->GetDescriptorSet(*shaderDataSetInfo, shaderDataSetBindings);
        passContext.GetCommandBuffer().bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipeineInfo.pipelineLayout, ShaderDataSetIndex, { shaderDataSet }, { shaderData.dynamicOffset });

        glm::uvec3 workGroupSize = shader->GetLocalSize();
        passContext.GetCommandBuffer().dispatch(
          uint32_t(sceneResources->volumeResolution.x / workGroupSize.x),
          uint32_t(sceneResources->volumeResolution.y / workGroupSize.y),
          uint32_t(sceneResources->volumeResolution.z / workGroupSize.z));
      }
    }));

    EnforceBoundaries(memoryPool, simulationData);

    core->GetRenderGraph()->AddPass(legit::RenderGraph::ComputePassDesc()
      .SetStorageImages({
        sceneResources->velocityVolumeProxy.imageViewProxy->Id(),
        sceneResources->divergenceVolumeProxy.imageViewProxy->Id()})
        .SetProfilerInfo(legit::Colors::emerald, "PassDivergence")
      .SetRecordFunc([this, memoryPool, simulationData](legit::RenderGraph::PassContext passContext)
    {
      auto shader = velocityDivergenceShader.compute.get();
      auto pipeineInfo = this->core->GetPipelineCache()->BindComputePipeline(passContext.GetCommandBuffer(), shader);
      {
        const legit::DescriptorSetLayoutKey *shaderDataSetInfo = shader->GetSetInfo(ShaderDataSetIndex);
        auto shaderData = memoryPool->BeginSet(shaderDataSetInfo);
        {
          auto shaderPassDataBuffer = memoryPool->GetUniformBufferData<SimulationData>("SimulationDataBuffer");
          *shaderPassDataBuffer = simulationData;
        }
        memoryPool->EndSet();

        std::vector<legit::StorageImageBinding> storageImageBindings;
        auto velocityVolumeProxy = passContext.GetImageView(sceneResources->velocityVolumeProxy.imageViewProxy->Id());
        storageImageBindings.push_back(shaderDataSetInfo->MakeStorageImageBinding("velocityImage", velocityVolumeProxy));
        auto divergenceVolumeProxy = passContext.GetImageView(sceneResources->divergenceVolumeProxy.imageViewProxy->Id());
        storageImageBindings.push_back(shaderDataSetInfo->MakeStorageImageBinding("divergenceImage", divergenceVolumeProxy));

        auto shaderDataSetBindings = legit::DescriptorSetBindings()
          .SetUniformBufferBindings(shaderData.uniformBufferBindings)
          .SetStorageImageBindings(storageImageBindings);

        auto shaderDataSet = this->core->GetDescriptorSetCache()->GetDescriptorSet(*shaderDataSetInfo, shaderDataSetBindings);
        passContext.GetCommandBuffer().bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipeineInfo.pipelineLayout, ShaderDataSetIndex, { shaderDataSet }, { shaderData.dynamicOffset });

        glm::uvec3 workGroupSize = shader->GetLocalSize();
        passContext.GetCommandBuffer().dispatch(
          uint32_t(sceneResources->volumeResolution.x / workGroupSize.x),
          uint32_t(sceneResources->volumeResolution.y / workGroupSize.y),
          uint32_t(sceneResources->volumeResolution.z / workGroupSize.z));
      }
    }));

    for(int i = 0; i < 5; i++)
    {
      for(int phase = 0; phase < 2; phase++)
      {
        core->GetRenderGraph()->AddPass(legit::RenderGraph::ComputePassDesc()
          .SetStorageImages({
            sceneResources->pressureVolumeProxy.imageViewProxy->Id(),
            sceneResources->divergenceVolumeProxy.imageViewProxy->Id() })
          .SetProfilerInfo(legit::Colors::emerald, "PassPoissonIteration")
          .SetRecordFunc([this, memoryPool, simulationData](legit::RenderGraph::PassContext passContext)
        {
          auto shader = poissonIterationShader.compute.get();
          auto pipeineInfo = this->core->GetPipelineCache()->BindComputePipeline(passContext.GetCommandBuffer(), shader);
          {
            const legit::DescriptorSetLayoutKey *shaderDataSetInfo = shader->GetSetInfo(ShaderDataSetIndex);
            auto shaderData = memoryPool->BeginSet(shaderDataSetInfo);
            {
              auto shaderPassDataBuffer = memoryPool->GetUniformBufferData<SimulationData>("SimulationDataBuffer");
              *shaderPassDataBuffer = simulationData;
            }
            memoryPool->EndSet();

            std::vector<legit::StorageImageBinding> storageImageBindings;
            auto pressureVolumeProxy = passContext.GetImageView(sceneResources->pressureVolumeProxy.imageViewProxy->Id());
            storageImageBindings.push_back(shaderDataSetInfo->MakeStorageImageBinding("pressureImage", pressureVolumeProxy));
            auto divergenceVolumeProxy = passContext.GetImageView(sceneResources->divergenceVolumeProxy.imageViewProxy->Id());
            storageImageBindings.push_back(shaderDataSetInfo->MakeStorageImageBinding("divergenceImage", divergenceVolumeProxy));

            auto shaderDataSetBindings = legit::DescriptorSetBindings()
              .SetUniformBufferBindings(shaderData.uniformBufferBindings)
              .SetStorageImageBindings(storageImageBindings);

            auto shaderDataSet = this->core->GetDescriptorSetCache()->GetDescriptorSet(*shaderDataSetInfo, shaderDataSetBindings);
            passContext.GetCommandBuffer().bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipeineInfo.pipelineLayout, ShaderDataSetIndex, { shaderDataSet }, { shaderData.dynamicOffset });

            glm::uvec3 workGroupSize = shader->GetLocalSize();
            passContext.GetCommandBuffer().dispatch(
              uint32_t(sceneResources->volumeResolution.x / workGroupSize.x / 2), //because of checkerboard
              uint32_t(sceneResources->volumeResolution.y / workGroupSize.y),
              uint32_t(sceneResources->volumeResolution.z / workGroupSize.z));
          }
        }));
        simulationData.iterationIndex++;
      }
    }

    core->GetRenderGraph()->AddPass(legit::RenderGraph::ComputePassDesc()
      .SetStorageImages({
        sceneResources->pressureVolumeProxy.imageViewProxy->Id(),
        sceneResources->velocityVolumeProxy.imageViewProxy->Id() })
      .SetProfilerInfo(legit::Colors::emerald, "PassApplyPressureGradient")
      .SetRecordFunc([this, memoryPool, simulationData](legit::RenderGraph::PassContext passContext)
    {
      auto shader = applyPressureGradientShader.compute.get();
      auto pipeineInfo = this->core->GetPipelineCache()->BindComputePipeline(passContext.GetCommandBuffer(), shader);
      {
        const legit::DescriptorSetLayoutKey *shaderDataSetInfo = shader->GetSetInfo(ShaderDataSetIndex);
        auto shaderData = memoryPool->BeginSet(shaderDataSetInfo);
        {
          auto shaderPassDataBuffer = memoryPool->GetUniformBufferData<SimulationData>("SimulationDataBuffer");
          *shaderPassDataBuffer = simulationData;
        }
        memoryPool->EndSet();

        std::vector<legit::StorageImageBinding> storageImageBindings;
        auto pressureVolumeProxy = passContext.GetImageView(sceneResources->pressureVolumeProxy.imageViewProxy->Id());
        storageImageBindings.push_back(shaderDataSetInfo->MakeStorageImageBinding("pressureImage", pressureVolumeProxy));
        auto velocityVolumeProxy = passContext.GetImageView(sceneResources->velocityVolumeProxy.imageViewProxy->Id());
        storageImageBindings.push_back(shaderDataSetInfo->MakeStorageImageBinding("velocityImage", velocityVolumeProxy));

        auto shaderDataSetBindings = legit::DescriptorSetBindings()
          .SetUniformBufferBindings(shaderData.uniformBufferBindings)
          .SetStorageImageBindings(storageImageBindings);

        auto shaderDataSet = this->core->GetDescriptorSetCache()->GetDescriptorSet(*shaderDataSetInfo, shaderDataSetBindings);
        passContext.GetCommandBuffer().bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipeineInfo.pipelineLayout, ShaderDataSetIndex, { shaderDataSet }, { shaderData.dynamicOffset });

        glm::uvec3 workGroupSize = shader->GetLocalSize();
        passContext.GetCommandBuffer().dispatch(
          uint32_t(sceneResources->volumeResolution.x / workGroupSize.x),
          uint32_t(sceneResources->volumeResolution.y / workGroupSize.y),
          uint32_t(sceneResources->volumeResolution.z / workGroupSize.z));
      }
    }));

    EnforceBoundaries(memoryPool, simulationData);

    core->GetRenderGraph()->AddPass(legit::RenderGraph::ComputePassDesc()
      .SetStorageBuffers({
        pointsDataProxyId })
      .SetInputImages({
        sceneResources->velocityVolumeProxy.imageViewProxy->Id() })
      .SetProfilerInfo(legit::Colors::emerald, "PassParticlesAdvect")
      .SetRecordFunc([this, memoryPool, simulationData, pointsDataProxyId](legit::RenderGraph::PassContext passContext)
    {
      auto shader = particlesAdvectShader.compute.get();
      auto pipeineInfo = this->core->GetPipelineCache()->BindComputePipeline(passContext.GetCommandBuffer(), shader);
      {
        const legit::DescriptorSetLayoutKey *shaderDataSetInfo = shader->GetSetInfo(ShaderDataSetIndex);
        auto shaderData = memoryPool->BeginSet(shaderDataSetInfo);
        {
          auto shaderPassDataBuffer = memoryPool->GetUniformBufferData<SimulationData>("SimulationDataBuffer");
          *shaderPassDataBuffer = simulationData;
        }
        memoryPool->EndSet();

        std::vector<legit::StorageBufferBinding> storageBufferBindings;
        auto pointsDataBuffer = passContext.GetBuffer(pointsDataProxyId);
        storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("PointsBuffer", pointsDataBuffer));

        std::vector<legit::ImageSamplerBinding> imageSamplerBindings;
        auto velocityImageView = passContext.GetImageView(sceneResources->velocityVolumeProxy.imageViewProxy->Id());
        imageSamplerBindings.push_back(shaderDataSetInfo->MakeImageSamplerBinding("velocitySampler", velocityImageView, linearSampler.get()));

        auto shaderDataSetBindings = legit::DescriptorSetBindings()
          .SetUniformBufferBindings(shaderData.uniformBufferBindings)
          .SetStorageBufferBindings(storageBufferBindings)
          .SetImageSamplerBindings(imageSamplerBindings);
        //.SetStorageImageBindings(storageImageBindings)


        auto shaderDataSet = this->core->GetDescriptorSetCache()->GetDescriptorSet(*shaderDataSetInfo, shaderDataSetBindings);
        passContext.GetCommandBuffer().bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipeineInfo.pipelineLayout, ShaderDataSetIndex, { shaderDataSet }, { shaderData.dynamicOffset });

        glm::uvec3 workGroupSize = shader->GetLocalSize();
        passContext.GetCommandBuffer().dispatch(uint32_t(simulationData.particlesCount / workGroupSize.x) + 1, 1, 1);
      }
    }));


    SolverBuffers res;
    res.velocityProxyId = sceneResources->velocityVolumeProxy.imageViewProxy->Id();

    return res;
  }


  void ReloadShaders()
  {
    fieldsInitShader.compute.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/WaterRenderer/Solvers/SimpleSolver/fieldsInit.comp.spv"));
    fieldsCopyShader.compute.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/WaterRenderer/Solvers/SimpleSolver/fieldsCopy.comp.spv"));
    fieldsAdvectShader.compute.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/WaterRenderer/Solvers/SimpleSolver/fieldsAdvect.comp.spv"));
    velocityDivergenceShader.compute.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/WaterRenderer/Solvers/velocityDivergence.comp.spv"));
    poissonIterationShader.compute.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/WaterRenderer/Solvers/poissonIteration.comp.spv"));
    applyPressureGradientShader.compute.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/WaterRenderer/Solvers/SimpleSolver/applyPressureGradient.comp.spv"));
    enforceBoundariesShader.compute.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/WaterRenderer/Solvers/SimpleSolver/enforceBoundaries.comp.spv"));
    particlesAdvectShader.compute.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/WaterRenderer/Solvers/particlesAdvect.comp.spv"));
  }
private:


  const static uint32_t ShaderDataSetIndex = 0;
  const static uint32_t DrawCallDataSetIndex = 1;


  struct SceneResources
  {
    SceneResources(legit::Core *core, glm::uvec3 _volumeResolution, glm::vec3 _volumeMin, glm::vec3 _volumeMax) :
      velocityVolumeProxy(core->GetRenderGraph(), vk::Format::eR32G32B32A32Sfloat, _volumeResolution, vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage),
      tmpVelocityVolumeProxy(core->GetRenderGraph(), vk::Format::eR32G32B32A32Sfloat, _volumeResolution, vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage),
      pressureVolumeProxy(core->GetRenderGraph(), vk::Format::eR32Sfloat, _volumeResolution, vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage),
      tmpPressureVolumeProxy(core->GetRenderGraph(), vk::Format::eR32Sfloat, _volumeResolution, vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage),
      divergenceVolumeProxy(core->GetRenderGraph(), vk::Format::eR32Sfloat, _volumeResolution, vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage)
    {
      this->volumeResolution = _volumeResolution;
      this->volumeMin = _volumeMin;
      this->volumeMax = _volumeMax;
    }
    VolumeProxy velocityVolumeProxy;
    VolumeProxy tmpVelocityVolumeProxy;
    VolumeProxy pressureVolumeProxy;
    VolumeProxy tmpPressureVolumeProxy;
    VolumeProxy divergenceVolumeProxy;

    glm::uvec3 volumeResolution;
    glm::vec3 volumeMin;
    glm::vec3 volumeMax;
  };
  std::unique_ptr<SceneResources> sceneResources;

  #pragma pack(push, 1)
  struct SimulationData
  {
    glm::uvec4 volumeResolution;
    glm::vec4 volumeMin;
    glm::vec4 volumeMax;
    glm::vec4 stepSize;
    glm::vec4 invStepSize;
    float timeStep;
    float h;
    glm::uint particlesCount;
    int iterationIndex;
  };
  #pragma pack(pop)

  void EnforceBoundaries(legit::ShaderMemoryPool *memoryPool, SimulationData simulationData)
  {
    core->GetRenderGraph()->AddPass(legit::RenderGraph::ComputePassDesc()
      .SetStorageImages({
        sceneResources->velocityVolumeProxy.imageViewProxy->Id() })
      .SetProfilerInfo(legit::Colors::emerald, "PassEnforceBoundaries")
      .SetRecordFunc([this, memoryPool, simulationData](legit::RenderGraph::PassContext passContext)
    {
      auto shader = enforceBoundariesShader.compute.get();
      auto pipeineInfo = this->core->GetPipelineCache()->BindComputePipeline(passContext.GetCommandBuffer(), shader);
      {
        const legit::DescriptorSetLayoutKey *shaderDataSetInfo = shader->GetSetInfo(ShaderDataSetIndex);
        auto shaderData = memoryPool->BeginSet(shaderDataSetInfo);
        {
          auto shaderPassDataBuffer = memoryPool->GetUniformBufferData<SimulationData>("SimulationDataBuffer");
          *shaderPassDataBuffer = simulationData;
        }
        memoryPool->EndSet();

        std::vector<legit::StorageImageBinding> storageImageBindings;
        auto velocityVolumeProxy = passContext.GetImageView(sceneResources->velocityVolumeProxy.imageViewProxy->Id());
        storageImageBindings.push_back(shaderDataSetInfo->MakeStorageImageBinding("velocityImage", velocityVolumeProxy));

        auto shaderDataSetBindings = legit::DescriptorSetBindings()
          .SetUniformBufferBindings(shaderData.uniformBufferBindings)
          .SetStorageImageBindings(storageImageBindings);

        auto shaderDataSet = this->core->GetDescriptorSetCache()->GetDescriptorSet(*shaderDataSetInfo, shaderDataSetBindings);
        passContext.GetCommandBuffer().bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipeineInfo.pipelineLayout, ShaderDataSetIndex, { shaderDataSet }, { shaderData.dynamicOffset });

        glm::uvec3 workGroupSize = shader->GetLocalSize();
        passContext.GetCommandBuffer().dispatch(
          uint32_t(sceneResources->volumeResolution.x / workGroupSize.x),
          uint32_t(sceneResources->volumeResolution.y / workGroupSize.y),
          uint32_t(sceneResources->volumeResolution.z / workGroupSize.z));
      }
    }));
  }

  struct FieldsInitShader
  {
    std::unique_ptr<legit::Shader> compute;
  } fieldsInitShader;

  struct FieldsCopy
  {
    std::unique_ptr<legit::Shader> compute;
  } fieldsCopyShader;

  struct FieldsAdvectShader
  {
    std::unique_ptr<legit::Shader> compute;
  } fieldsAdvectShader;

  struct VelocityDivergenceShader
  {
    std::unique_ptr<legit::Shader> compute;
  } velocityDivergenceShader;

  struct PoissonIterationShader
  {
    std::unique_ptr<legit::Shader> compute;
  } poissonIterationShader;

  struct ApplyPressureGradientShader
  {
    std::unique_ptr<legit::Shader> compute;
  } applyPressureGradientShader;

  struct EnforceBoundariesShader
  {
    std::unique_ptr<legit::Shader> compute;
  } enforceBoundariesShader;

  struct ParticlesAdvectShader
  {
    std::unique_ptr<legit::Shader> compute;
  } particlesAdvectShader;

  std::unique_ptr<legit::Sampler> linearSampler;

  std::default_random_engine eng;
  std::uniform_real_distribution<float> dis{ 0.0f, 1.0f };

  legit::Core *core;
};


/*using PointIndex = size_t;
struct Point
{
  float data;
  PointIndex next;
};

size_t GetListSize(Point *points, PointIndex head)
{
  size_t count = 0;
  PointIndex curr = head;
  while (curr != PointIndex(-1))
  {
    count++;
    curr = points[curr].next;
  }
  return count;
}

struct MergeResult
{
  PointIndex head;
  PointIndex tail;
};

MergeResult MergeLists(Point *points, PointIndex leftHead, PointIndex rightHead)
{
  assert(leftHead != PointIndex(-1) && rightHead != PointIndex(-1));
  PointIndex currLeft = leftHead;
  PointIndex currRight = rightHead;
  MergeResult res;
  if (points[currLeft].data < points[currRight].data)
  {
    res.head = currLeft;
    currLeft = points[currLeft].next;
  }
  else
  {
    res.head = currRight;
    currRight = points[currRight].next;
  }
  PointIndex currMerged = res.head;
  while (currLeft != PointIndex(-1) || currRight != PointIndex(-1))
  {
    //itCount++; //~10000 iterations for random 1000-element array
    if (currRight == PointIndex(-1) || (currLeft != PointIndex(-1) && points[currLeft].data < points[currRight].data))
    {
      points[currMerged].next = currLeft;
      currMerged = currLeft;
      currLeft = points[currLeft].next;
    }
    else
    {
      points[currMerged].next = currRight;
      currMerged = currRight;
      currRight = points[currRight].next;
    }
  }
  points[currMerged].next = PointIndex(-1);
  res.tail = currMerged;
  return res;
}

PointIndex SeparateList(Point *points, PointIndex head, size_t count)
{
  assert(head != PointIndex(-1));
  PointIndex curr = head;
  for (size_t i = 0; i < count - 1 && points[curr].next != PointIndex(-1); i++)
    curr = points[curr].next;
  PointIndex nextHead = points[curr].next;
  points[curr].next = PointIndex(-1);
  return nextHead;
}

PointIndex MergeSort(Point *points, PointIndex head)
{
  size_t count = GetListSize(points, head);
  for (size_t gap = 1; gap < count; gap *= 2)
  {
    PointIndex lastTail = PointIndex(-1);
    PointIndex curr = head;
    while (curr != PointIndex(-1))
    {
      PointIndex leftHead = curr;
      PointIndex rightHead = SeparateList(points, leftHead, gap);
      if (rightHead == PointIndex(-1))
        break;

      PointIndex nextHead = SeparateList(points, rightHead, gap);

      MergeResult mergeResult = MergeLists(points, leftHead, rightHead);
      assert(mergeResult.head != PointIndex(-1));
      assert(mergeResult.tail != PointIndex(-1));
      if (lastTail != PointIndex(-1))
        points[lastTail].next = mergeResult.head;
      points[mergeResult.tail].next = nextHead;
      lastTail = mergeResult.tail;
      if (curr == head)
        head = mergeResult.head;
      curr = nextHead;
    }
  }
  return head;
}

PointIndex GetPreMinNode(Point *points, PointIndex head)
{
  PointIndex minIndex = head;
  PointIndex preMinIndex = PointIndex(-1);
  for (PointIndex curr = head; points[curr].next != PointIndex(-1); curr = points[curr].next)
  {
    if (points[points[curr].next].data < points[minIndex].data)
    {
      minIndex = points[curr].next;
      preMinIndex = curr;
    }
  }
  return preMinIndex;
}
PointIndex BubbleSort(Point *points, PointIndex head)
{
  bool wasChanged;
  do
  {
    PointIndex curr = head;
    PointIndex prev = PointIndex(-1);
    PointIndex next = points[head].next;
    wasChanged = false;
    while (next != PointIndex(-1)) 
    {
      if (points[curr].data > points[next].data)
      {
        wasChanged = true;

        if (prev != PointIndex(-1))
        {
          PointIndex tmp = points[next].next;

          points[prev].next = next;
          points[next].next = curr;
          points[curr].next = tmp;
        }
        else 
        {
          PointIndex tmp = points[next].next;

          head = next;
          points[next].next = curr;
          points[curr].next = tmp;
        }

        prev = next;
        next = points[curr].next;
      }
      else
      {
        prev = curr;
        curr = next;
        next = points[next].next;
      }
    }
  } while (wasChanged);
  return head;
}

PointIndex SortedInsert(Point *points, PointIndex head, PointIndex newPoint)
{
  if (head == PointIndex(-1) || points[head].data >= points[newPoint].data)
  {
    points[newPoint].next = head;
    head = newPoint;
  }
  else
  {
    PointIndex curr = head;
    for (; points[curr].next != PointIndex(-1) && points[points[curr].next].data < points[newPoint].data; curr = points[curr].next);
    points[newPoint].next = points[curr].next;
    points[curr].next = newPoint;
  }
  return head;
}
PointIndex InsertionSort(Point *points, PointIndex head)
{
  PointIndex newHead = PointIndex(-1);
  PointIndex curr = head;
  while (curr != PointIndex(-1))
  {
    PointIndex next = points[curr].next;
    newHead = SortedInsert(points, newHead, curr);
    curr = next;
  }
  return newHead;
}
void PrintList(Point *points, PointIndex head)
{
  size_t index = 0;
  float last = -1.0f;
  for(PointIndex curr = head; curr != PointIndex(-1); curr = points[curr].next)
  {
    std::cout << "[" << index << "]: " << points[curr].data << "\n";
    if (last > points[curr].data)
      std::cout << "\n\nFAYYYUUUL\n\n";
    last = points[curr].data;
    index++;
  }
}
void TestSort()
{
  std::default_random_engine eng;
  std::uniform_real_distribution<float> dis(0.0f, 1.0f);

  size_t pointsCount = 64;
  size_t trialsCount = 100000;

  std::vector<Point> points;
  for (size_t i = 0; i < pointsCount; i++)
  {
    if (points.size() > 0)
      points.back().next = points.size();
    Point point;
    point.data = dis(eng);
    point.next = PointIndex(-1);
    points.push_back(point);
  }
  using hrc = std::chrono::high_resolution_clock;

  std::vector<Point> bckpPoints = points;

  hrc::time_point t1 = hrc::now();
  float test1 = 0;
  for (int i = 0; i < trialsCount; i++)
  {
    points = bckpPoints;
    PointIndex newHead = BubbleSort(points.data(), PointIndex(0));
    test1 += points[newHead].data;
  }
  hrc::time_point t2 = hrc::now();
  float test2 = 0;
  for (int i = 0; i < trialsCount; i++)
  {
    points = bckpPoints;
    PointIndex newHead = MergeSort(points.data(), PointIndex(0));
    test2 += points[newHead].data;
  }
  hrc::time_point t3 = hrc::now();
  float test3 = 0;
  PointIndex newHead;
  for (int i = 0; i < trialsCount; i++)
  {
    points = bckpPoints;
    newHead = InsertionSort(points.data(), PointIndex(0));
    test3 += points[newHead].data;
  }
  hrc::time_point t4 = hrc::now();

  std::cout << "Bubble : " << std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count() << "us\n";
  std::cout << "Merge: " << std::chrono::duration_cast<std::chrono::microseconds>(t3 - t2).count() << "us\n";
  std::cout << "Insertion: " << std::chrono::duration_cast<std::chrono::microseconds>(t4 - t3).count() << "us\n";
  std::cout << "test1:" << test1 << " test2: " << test2 << " test3: " << test3 << "\n";

  PrintList(points.data(), newHead);
}

void TestSum()
{
  std::default_random_engine eng;
  std::uniform_int_distribution<size_t> dis(0, 10);

  size_t bucketsCount = 100;

  struct Bucket
  {
    size_t size;
    size_t offset;
  };
  std::vector<Bucket> buckets;
  buckets.resize(bucketsCount);
  for (size_t i = 0; i < buckets.size(); i++)
  {
    buckets[i].offset = 0;
    buckets[i].size = dis(eng);
    buckets[i].offset = buckets[i].size;
  }

  size_t stepSize = 1;
  for(;stepSize < buckets.size(); stepSize *= 2)
  {
    for (size_t i = 0; i + stepSize < buckets.size(); i += stepSize * 2)
    {
      buckets[i].offset = buckets[i].offset + buckets[i + stepSize].offset;
    }
  }
  stepSize /= 2;
  for (; stepSize > 0; stepSize /= 2)
  {
    for (size_t i = 0; i + stepSize < buckets.size(); i += stepSize * 2)
    {
      size_t sum = buckets[i].offset;
      buckets[i].offset = sum - buckets[i + stepSize].of?????fset;
      buckets[i + stepSize].offset = sum;
    }
  }

  int p = 1;
}*/