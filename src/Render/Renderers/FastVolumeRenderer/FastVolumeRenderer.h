#include "../../Common/MipBuilder.h"
#include "../../Common/BlurBuilder.h"
#include "../../Common/DebugRenderer.h"
#include "../WaterRenderer/ShrodingerSolver.h"


class FastVolumeRenderer
{
public:
  FastVolumeRenderer(legit::Core *_core) :
    mipBuilder(_core),
    shrodingerSolver(_core)
  {
    this->core = _core;

    vertexDecl = Mesh::GetVertexDeclaration();

    volumeSampler.reset(new legit::Sampler(core->GetLogicalDevice(), vk::SamplerAddressMode::eClampToEdge, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear));
    cubemapSampler.reset(new legit::Sampler(core->GetLogicalDevice(), vk::SamplerAddressMode::eClampToEdge, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear));

    {
      //auto texelData = legit::LoadKtxFromFile("../data/Textures/Cubemaps/1_16f.ktx");
      auto texelData = legit::LoadKtxFromFile("../data/Textures/Cubemaps/mipped_3_16f.ktx");
      //auto texelData = legit::LoadKtxFromFile("../data/Textures/Cubemaps/5_16f.ktx");
      auto cubemapCreateDesc = legit::Image::CreateInfoCube(texelData.baseSize, uint32_t(texelData.mips.size()), texelData.format, vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst);
      this->specularCubemap = std::unique_ptr<legit::Image>(new legit::Image(core->GetPhysicalDevice(), core->GetLogicalDevice(), cubemapCreateDesc));
      legit::LoadTexelData(core, &texelData, specularCubemap->GetImageData());
      specularCubemapView = std::unique_ptr<legit::ImageView>(new legit::ImageView(core->GetLogicalDevice(), specularCubemap->GetImageData(), 0, specularCubemap->GetImageData()->GetMipsCount()));
    }


    ReloadShaders();
  }

  void RecreateSceneResources(Scene *scene)
  {
    float size = 2.5f;
    sceneResources.reset(new SceneResources(core, glm::uvec3(128, 128, 128), glm::vec3(-size, -size, -size), glm::vec3(size, size, size)));
    shrodingerSolver.RecreateSceneResources(sceneResources->volumeResolution, sceneResources->aabbMin, sceneResources->aabbMax);
  }
  void RecreateSwapchainResources(vk::Extent2D viewportExtent, size_t inFlightFramesCount)
  {
    viewportResources.reset(new ViewportResources(viewportExtent));
  }
  void RenderFrame(const legit::InFlightQueue::FrameInfo &frameInfo, const Camera &camera, const Camera &light, Scene *scene, GLFWwindow *window)
  {
    static float time = 0.0f;
    time += 0.01f;

    VolumeDataBuffer volumeDataBuffer;
    float aspect = float(viewportResources->extent.width) / float(viewportResources->extent.height);
    volumeDataBuffer.viewMatrix = glm::inverse(camera.GetTransformMatrix());
    volumeDataBuffer.projMatrix = glm::perspective(1.0f, aspect, 0.01f, 1000.0f) * glm::scale(glm::vec3(1.0f, -1.0f, -1.0f));
    float size = 2.5f;
    volumeDataBuffer.aabbMin = glm::vec4(glm::vec3(-size, -size, -size), 0.0f);
    volumeDataBuffer.aabbMax = glm::vec4(glm::vec3( size,  size,  size), 0.0f);
    volumeDataBuffer.resolution = glm::uvec4(sceneResources->volumeResolution, 0);
    volumeDataBuffer.viewportExtent = glm::vec4(viewportResources->extent.width, viewportResources->extent.height, 0.0f, 0.0f);
    volumeDataBuffer.castDir = glm::vec4(0.0f);
    volumeDataBuffer.patternSize = glm::ivec4(glm::ivec3(4), 0);
    volumeDataBuffer.time = time;
    static float densityMult = 5.0f;
    ImGui::SliderFloat("DensityMult", &densityMult, 0.0f, 15.0f);
    volumeDataBuffer.densityMult = densityMult;
    volumeDataBuffer.emissionMult = 0.0f;

    static float maxStepTransparency = 0.9f;
    ImGui::SliderFloat("MaxStepTransparency", &maxStepTransparency, 0.0f, 1.0f);
    volumeDataBuffer.maxStepTransparency = maxStepTransparency;

    static bool isFirst = true;
    if (ImGui::Button("Load data") || isFirst)
      InitVolumes(frameInfo, volumeDataBuffer);

    auto res = shrodingerSolver.Update(frameInfo.memoryPool);
    LoadVolume(frameInfo, volumeDataBuffer, res.velocityProxyId, res.waveFuncProxyId);

    mipBuilder.BuildMips(core->GetRenderGraph(), frameInfo.memoryPool, sceneResources->iDensityProxy);
    DeinterleaveDensity(frameInfo, volumeDataBuffer);
    isFirst = false;
    //TestRun(frameInfo, volumeDataBuffer);

    //BuildStepMap(frameInfo, volumeDataBuffer);
    //RenderIndirectLight(frameInfo, volumeDataBuffer);
    RenderSweepingIndirectLight(frameInfo, volumeDataBuffer);
    InterleaveIndirectLight(frameInfo, volumeDataBuffer);

    RenderVolume(frameInfo, volumeDataBuffer);
    AddTransitionPass(res.velocityProxyId, res.waveFuncProxyId);
  }

  void ReloadShaders()
  {
    volumeRendererShader.vertex.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/Common/screenspaceQuad.vert.spv"));
    volumeRendererShader.fragment.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/FastVolumeRenderer/volumeRenderer.frag.spv"));
    volumeRendererShader.program.reset(new legit::ShaderProgram(volumeRendererShader.vertex.get(), volumeRendererShader.fragment.get()));

    volumesInitShader.compute.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/FastVolumeRenderer/volumeInit.comp.spv"));
    volumeLoadShader.compute.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/FastVolumeRenderer/volumeLoad.comp.spv"));
    stepMapShader.compute.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/FastVolumeRenderer/stepMapBuilder.comp.spv"));
    indirectLightShader.compute.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/FastVolumeRenderer/volumeCaster.comp.spv"));
    //indirectLightShader.compute.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/FastVolumeRenderer/test.comp.spv"));
    volumeSweeperShader.compute.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/FastVolumeRenderer/volumeSweeper.comp.spv"));
    densityDeinterleaveShader.compute.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/FastVolumeRenderer/densityDeinterleave.comp.spv"));
    indirectLightInterleaveShader.compute.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/FastVolumeRenderer/indirectLightInterleave.comp.spv"));
    mipBuilder.ReloadShaders();
    shrodingerSolver.ReloadShaders();
  }
private:
  const static uint32_t ShaderDataSetIndex = 0;
  const static uint32_t DrawCallDataSetIndex = 1;

  legit::VertexDeclaration vertexDecl;

  #pragma pack(push, 1)
  struct VolumeDataBuffer
  {
    glm::mat4 viewMatrix;
    glm::mat4 projMatrix;
    glm::vec4 viewportExtent;
    glm::uvec4 resolution;
    glm::vec4 aabbMin;
    glm::vec4 aabbMax;
    glm::vec4 castDir;
    glm::ivec4 patternSize;
    float time;
    float densityMult;
    float emissionMult;
    float maxStepTransparency;
  };
  #pragma pack(pop)

  void RenderVolume(const legit::InFlightQueue::FrameInfo &frameInfo, VolumeDataBuffer volumeDataBuffer)
  {
    core->GetRenderGraph()->AddPass(legit::RenderGraph::RenderPassDesc()
      .SetColorAttachments({ frameInfo.swapchainImageViewProxyId }, vk::AttachmentLoadOp::eClear)
      .SetInputImages(
      {
        sceneResources->iDensityProxy.mippedImageViewProxy->Id(),
        sceneResources->stepMapProxy.mips[0].imageViewProxy->Id(),
        sceneResources->iIndirectLightProxy.mips[0].imageViewProxy->Id()
      })
      .SetRenderAreaExtent(viewportResources->extent)
      .SetProfilerInfo(legit::Colors::peterRiver, "VolumeRendering")
      .SetRecordFunc([this, frameInfo, volumeDataBuffer](legit::RenderGraph::RenderPassContext passContext)
    {
      auto shader = this->volumeRendererShader.program.get();
      auto pipeineInfo = this->core->GetPipelineCache()->BindGraphicsPipeline(passContext.GetCommandBuffer(), passContext.GetRenderPass()->GetHandle(), legit::DepthSettings::Disabled(), { legit::BlendSettings::Opaque() }, legit::VertexDeclaration(), vk::PrimitiveTopology::eTriangleFan, shader);
      {
        const legit::DescriptorSetLayoutKey *shaderDataSetInfo = shader->GetSetInfo(ShaderDataSetIndex);
        auto shaderDataSetUniforms = frameInfo.memoryPool->BeginSet(shaderDataSetInfo);
        {
          auto mappedVolumeDataBuffer= frameInfo.memoryPool->GetUniformBufferData<VolumeDataBuffer>("VolumeDataBuffer");
          *mappedVolumeDataBuffer = volumeDataBuffer;
        }
        frameInfo.memoryPool->EndSet();

        std::vector<legit::ImageSamplerBinding> imageSamplerBindings;
        imageSamplerBindings.push_back(shaderDataSetInfo->MakeImageSamplerBinding("specularCubemap", this->specularCubemapView.get(), this->cubemapSampler.get()));
        auto densityDataView = passContext.GetImageView(sceneResources->iDensityProxy.mippedImageViewProxy->Id());
        imageSamplerBindings.push_back(shaderDataSetInfo->MakeImageSamplerBinding("densitySampler", densityDataView, this->volumeSampler.get()));
        auto stepMapView = passContext.GetImageView(this->sceneResources->stepMapProxy.mips[0].imageViewProxy->Id());
        imageSamplerBindings.push_back(shaderDataSetInfo->MakeImageSamplerBinding("stepMapSampler", stepMapView, this->volumeSampler.get()));
        auto iIndirectLightDataView = passContext.GetImageView(sceneResources->iIndirectLightProxy.mips[0].imageViewProxy->Id());
        imageSamplerBindings.push_back(shaderDataSetInfo->MakeImageSamplerBinding("indirectLightSampler", iIndirectLightDataView, this->volumeSampler.get()));


        auto shaderDataSetBindings = legit::DescriptorSetBindings()
          .SetUniformBufferBindings(shaderDataSetUniforms.uniformBufferBindings)
          .SetImageSamplerBindings(imageSamplerBindings);

        auto shaderDataSet = this->core->GetDescriptorSetCache()->GetDescriptorSet(*shaderDataSetInfo, shaderDataSetBindings);

        passContext.GetCommandBuffer().bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeineInfo.pipelineLayout, ShaderDataSetIndex, { shaderDataSet }, { shaderDataSetUniforms.dynamicOffset });
        passContext.GetCommandBuffer().draw(4, 1, 0, 0);
      }
    }));
  }

  void InitVolumes(const legit::InFlightQueue::FrameInfo &frameInfo, VolumeDataBuffer volumeDataBuffer)
  {
    core->GetRenderGraph()->AddPass(legit::RenderGraph::ComputePassDesc()
      .SetStorageImages(
      {
        sceneResources->iDensityProxy.mips[0].imageViewProxy->Id(),
        sceneResources->iIndirectLightProxy.mips[0].imageViewProxy->Id()
      })
      .SetProfilerInfo(legit::Colors::clouds, "Loading data")
      .SetRecordFunc([this, frameInfo, volumeDataBuffer](legit::RenderGraph::PassContext passContext)
    {
      auto shader = this->volumesInitShader.compute.get();
      auto pipeineInfo = this->core->GetPipelineCache()->BindComputePipeline(passContext.GetCommandBuffer(), shader);
      {
        const legit::DescriptorSetLayoutKey *shaderDataSetInfo = shader->GetSetInfo(ShaderDataSetIndex);
        auto shaderDataSetUniforms = frameInfo.memoryPool->BeginSet(shaderDataSetInfo);
        {
          auto mappedVolumeDataBuffer= frameInfo.memoryPool->GetUniformBufferData<VolumeDataBuffer>("VolumeDataBuffer");
          *mappedVolumeDataBuffer = volumeDataBuffer;
        }
        frameInfo.memoryPool->EndSet();

        std::vector<legit::StorageImageBinding> storageImageBindings;
        auto densityDataView = passContext.GetImageView(sceneResources->iDensityProxy.mips[0].imageViewProxy->Id());
        storageImageBindings.push_back(shaderDataSetInfo->MakeStorageImageBinding("densityImage", densityDataView));

        auto indirectLightDataView = passContext.GetImageView(sceneResources->iIndirectLightProxy.mips[0].imageViewProxy->Id());
        storageImageBindings.push_back(shaderDataSetInfo->MakeStorageImageBinding("dIndirectLightImage", indirectLightDataView));

        auto shaderDataSetBindings = legit::DescriptorSetBindings()
          .SetUniformBufferBindings(shaderDataSetUniforms.uniformBufferBindings)
          .SetStorageImageBindings(storageImageBindings);

        auto shaderDataSet = this->core->GetDescriptorSetCache()->GetDescriptorSet(*shaderDataSetInfo, shaderDataSetBindings);
        passContext.GetCommandBuffer().bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipeineInfo.pipelineLayout, ShaderDataSetIndex, { shaderDataSet }, { shaderDataSetUniforms.dynamicOffset });

        glm::uvec3 workGroupSize = shader->GetLocalSize();
        passContext.GetCommandBuffer().dispatch(
          uint32_t(sceneResources->volumeResolution.x / workGroupSize.x),
          uint32_t(sceneResources->volumeResolution.y / workGroupSize.y),
          uint32_t(sceneResources->volumeResolution.z / workGroupSize.z));
      }
    }));
  }

  void LoadVolume(const legit::InFlightQueue::FrameInfo &frameInfo, VolumeDataBuffer volumeDataBuffer, legit::RenderGraph::ImageViewProxyId velocityProxyId, legit::RenderGraph::ImageViewProxyId waveFuncProxyId)
  {
    core->GetRenderGraph()->AddPass(legit::RenderGraph::ComputePassDesc()
      .SetInputImages({ velocityProxyId , waveFuncProxyId })
      .SetStorageImages(
      {
        sceneResources->iDensityProxy.mips[0].imageViewProxy->Id(),
      })
      .SetProfilerInfo(legit::Colors::clouds, "Loading volumes")
      .SetRecordFunc([this, frameInfo, volumeDataBuffer, velocityProxyId, waveFuncProxyId](legit::RenderGraph::PassContext passContext)
    {
      auto shader = this->volumeLoadShader.compute.get();
      auto pipeineInfo = this->core->GetPipelineCache()->BindComputePipeline(passContext.GetCommandBuffer(), shader);
      {
        const legit::DescriptorSetLayoutKey *shaderDataSetInfo = shader->GetSetInfo(ShaderDataSetIndex);
        auto shaderDataSetUniforms = frameInfo.memoryPool->BeginSet(shaderDataSetInfo);
        {
          auto mappedVolumeDataBuffer= frameInfo.memoryPool->GetUniformBufferData<VolumeDataBuffer>("VolumeDataBuffer");
          *mappedVolumeDataBuffer = volumeDataBuffer;
        }
        frameInfo.memoryPool->EndSet();

        std::vector<legit::ImageSamplerBinding> imageSamplerBindings;
        auto velocityView = passContext.GetImageView(velocityProxyId);
        imageSamplerBindings.push_back(shaderDataSetInfo->MakeImageSamplerBinding("velocitySampler", velocityView, this->volumeSampler.get()));
        auto waveFuncView = passContext.GetImageView(waveFuncProxyId);
        imageSamplerBindings.push_back(shaderDataSetInfo->MakeImageSamplerBinding("waveFuncSampler", waveFuncView, this->volumeSampler.get()));

        std::vector<legit::StorageImageBinding> storageImageBindings;
        auto densityDataView = passContext.GetImageView(sceneResources->iDensityProxy.mips[0].imageViewProxy->Id());
        storageImageBindings.push_back(shaderDataSetInfo->MakeStorageImageBinding("densityImage", densityDataView));

        auto shaderDataSetBindings = legit::DescriptorSetBindings()
          .SetUniformBufferBindings(shaderDataSetUniforms.uniformBufferBindings)
          .SetImageSamplerBindings(imageSamplerBindings)
          .SetStorageImageBindings(storageImageBindings);

        auto shaderDataSet = this->core->GetDescriptorSetCache()->GetDescriptorSet(*shaderDataSetInfo, shaderDataSetBindings);
        passContext.GetCommandBuffer().bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipeineInfo.pipelineLayout, ShaderDataSetIndex, { shaderDataSet }, { shaderDataSetUniforms.dynamicOffset });

        glm::uvec3 workGroupSize = shader->GetLocalSize();
        passContext.GetCommandBuffer().dispatch(
          uint32_t(sceneResources->volumeResolution.x / workGroupSize.x),
          uint32_t(sceneResources->volumeResolution.y / workGroupSize.y),
          uint32_t(sceneResources->volumeResolution.z / workGroupSize.z));
      }
    }));
  }


  void BuildStepMap(const legit::InFlightQueue::FrameInfo &frameInfo, VolumeDataBuffer volumeDataBuffer)
  {
    core->GetRenderGraph()->AddPass(legit::RenderGraph::ComputePassDesc()
      .SetStorageImages(
      {
        sceneResources->stepMapProxy.mips[0].imageViewProxy->Id()
      })
      .SetInputImages({ sceneResources->iDensityProxy.mippedImageViewProxy->Id() })
      .SetProfilerInfo(legit::Colors::clouds, "Step map")
      .SetRecordFunc([this, frameInfo, volumeDataBuffer](legit::RenderGraph::PassContext passContext)
    {
      auto shader = this->stepMapShader.compute.get();
      auto pipeineInfo = this->core->GetPipelineCache()->BindComputePipeline(passContext.GetCommandBuffer(), shader);
      {
        const legit::DescriptorSetLayoutKey *shaderDataSetInfo = shader->GetSetInfo(ShaderDataSetIndex);
        auto shaderDataSetUniforms = frameInfo.memoryPool->BeginSet(shaderDataSetInfo);
        {
          auto mappedVolumeDataBuffer= frameInfo.memoryPool->GetUniformBufferData<VolumeDataBuffer>("VolumeDataBuffer");
          *mappedVolumeDataBuffer = volumeDataBuffer;
        }
        frameInfo.memoryPool->EndSet();

        std::vector<legit::ImageSamplerBinding> imageSamplerBindings;
        auto densityDataView = passContext.GetImageView(sceneResources->iDensityProxy.mippedImageViewProxy->Id());
        imageSamplerBindings.push_back(shaderDataSetInfo->MakeImageSamplerBinding("densitySampler", densityDataView, this->volumeSampler.get()));

        std::vector<legit::StorageImageBinding> storageImageBindings;
        auto stepMapView = passContext.GetImageView(sceneResources->stepMapProxy.mips[0].imageViewProxy->Id());
        storageImageBindings.push_back(shaderDataSetInfo->MakeStorageImageBinding("stepMapImage", stepMapView));

        auto shaderDataSetBindings = legit::DescriptorSetBindings()
          .SetUniformBufferBindings(shaderDataSetUniforms.uniformBufferBindings)
          .SetImageSamplerBindings(imageSamplerBindings)
          .SetStorageImageBindings(storageImageBindings);

        auto shaderDataSet = this->core->GetDescriptorSetCache()->GetDescriptorSet(*shaderDataSetInfo, shaderDataSetBindings);
        passContext.GetCommandBuffer().bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipeineInfo.pipelineLayout, ShaderDataSetIndex, { shaderDataSet }, { shaderDataSetUniforms.dynamicOffset });

        glm::uvec3 workGroupSize = shader->GetLocalSize();
        passContext.GetCommandBuffer().dispatch(
          uint32_t(sceneResources->volumeResolution.x / workGroupSize.x),
          uint32_t(sceneResources->volumeResolution.y / workGroupSize.y),
          uint32_t(sceneResources->volumeResolution.z / workGroupSize.z));
      }
    }));
  }

  glm::vec3 GetRandomSpherePoint()
  {
    glm::vec3 point;
    do
    {
      point = glm::vec3(dis(eng), dis(eng), dis(eng)) * 2.0f - 1.0f;
    } while (glm::dot(point, point) > 1.0);

    return glm::normalize(point);
  }

  void RenderIndirectLight(const legit::InFlightQueue::FrameInfo &frameInfo, VolumeDataBuffer volumeDataBuffer)
  {
    static float ang = 0.0f;
    ang += 0.01f;
    volumeDataBuffer.castDir = glm::vec4(GetRandomSpherePoint(), 0.0f);
    //volumeDataBuffer.castDir = glm::vec4(cos(ang), 0.0f, sin(ang), 0.0f);
    core->GetRenderGraph()->AddPass(legit::RenderGraph::ComputePassDesc()
      .SetInputImages(
      {
        sceneResources->iDensityProxy.mippedImageViewProxy->Id(),
        sceneResources->stepMapProxy.mips[0].imageViewProxy->Id()
      })
      .SetStorageImages(
      {
        sceneResources->iIndirectLightProxy.mips[0].imageViewProxy->Id(),
        sceneResources->dIndirectLightProxy.mips[0].imageViewProxy->Id()
      })
      .SetProfilerInfo(legit::Colors::clouds, "Indirect light")
      .SetRecordFunc([this, frameInfo, volumeDataBuffer](legit::RenderGraph::PassContext passContext)
    {
      auto shader = this->indirectLightShader.compute.get();
      auto pipeineInfo = this->core->GetPipelineCache()->BindComputePipeline(passContext.GetCommandBuffer(), shader);
      {
        const legit::DescriptorSetLayoutKey *shaderDataSetInfo = shader->GetSetInfo(ShaderDataSetIndex);
        auto shaderDataSetUniforms = frameInfo.memoryPool->BeginSet(shaderDataSetInfo);
        {
          auto mappedVolumeDataBuffer= frameInfo.memoryPool->GetUniformBufferData<VolumeDataBuffer>("VolumeDataBuffer");
          *mappedVolumeDataBuffer = volumeDataBuffer;
        }
        frameInfo.memoryPool->EndSet();


        std::vector<legit::ImageSamplerBinding> imageSamplerBindings;
        imageSamplerBindings.push_back(shaderDataSetInfo->MakeImageSamplerBinding("specularCubemap", this->specularCubemapView.get(), this->cubemapSampler.get()));
        auto densityDataView = passContext.GetImageView(sceneResources->iDensityProxy.mippedImageViewProxy->Id());
        imageSamplerBindings.push_back(shaderDataSetInfo->MakeImageSamplerBinding("densitySampler", densityDataView, this->volumeSampler.get()));
        auto stepMapView = passContext.GetImageView(this->sceneResources->stepMapProxy.mips[0].imageViewProxy->Id());
        imageSamplerBindings.push_back(shaderDataSetInfo->MakeImageSamplerBinding("stepMapSampler", stepMapView, this->volumeSampler.get()));


        std::vector<legit::StorageImageBinding> storageImageBindings;
        auto indirectLightPreciseDataView = passContext.GetImageView(sceneResources->dIndirectLightProxy.mips[0].imageViewProxy->Id());
        storageImageBindings.push_back(shaderDataSetInfo->MakeStorageImageBinding("indirectLightPreciseImage", indirectLightPreciseDataView));
        auto indirectLightDataView = passContext.GetImageView(sceneResources->iIndirectLightProxy.mips[0].imageViewProxy->Id());
        storageImageBindings.push_back(shaderDataSetInfo->MakeStorageImageBinding("indirectLightImage", indirectLightDataView));

        auto shaderDataSetBindings = legit::DescriptorSetBindings()
          .SetUniformBufferBindings(shaderDataSetUniforms.uniformBufferBindings)
          .SetImageSamplerBindings(imageSamplerBindings)
          .SetStorageImageBindings(storageImageBindings);

        auto shaderDataSet = this->core->GetDescriptorSetCache()->GetDescriptorSet(*shaderDataSetInfo, shaderDataSetBindings);
        passContext.GetCommandBuffer().bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipeineInfo.pipelineLayout, ShaderDataSetIndex, { shaderDataSet }, { shaderDataSetUniforms.dynamicOffset });

        glm::uvec3 workGroupSize = shader->GetLocalSize();
        passContext.GetCommandBuffer().dispatch(
          uint32_t(sceneResources->volumeResolution.x / workGroupSize.x), //because of checkerboard
          uint32_t(sceneResources->volumeResolution.y / workGroupSize.y),
          uint32_t(sceneResources->volumeResolution.z / workGroupSize.z));
      }
    }));
  }

  void RenderSweepingIndirectLight(const legit::InFlightQueue::FrameInfo &frameInfo, VolumeDataBuffer volumeDataBuffer)
  {
    static float ang = 0.0f;
    ang += 0.01f;
    volumeDataBuffer.castDir = glm::vec4(GetRandomSpherePoint(), 0.0f);
    //volumeDataBuffer.castDir = glm::vec4(cos(ang), 0.0f, sin(ang), 0.0f);
    core->GetRenderGraph()->AddPass(legit::RenderGraph::ComputePassDesc()
      .SetInputImages(
      {
        sceneResources->dDensityProxy.mippedImageViewProxy->Id(),
      })
      .SetStorageImages(
      {
        sceneResources->dIndirectLightProxy.mips[0].imageViewProxy->Id()
      })
      .SetProfilerInfo(legit::Colors::clouds, "Indirect light")
      .SetRecordFunc([this, frameInfo, volumeDataBuffer](legit::RenderGraph::PassContext passContext)
    {
      auto shader = this->volumeSweeperShader.compute.get();
      auto pipeineInfo = this->core->GetPipelineCache()->BindComputePipeline(passContext.GetCommandBuffer(), shader);
      {
        const legit::DescriptorSetLayoutKey *shaderDataSetInfo = shader->GetSetInfo(ShaderDataSetIndex);
        auto shaderDataSetUniforms = frameInfo.memoryPool->BeginSet(shaderDataSetInfo);
        {
          auto mappedVolumeDataBuffer= frameInfo.memoryPool->GetUniformBufferData<VolumeDataBuffer>("VolumeDataBuffer");
          *mappedVolumeDataBuffer = volumeDataBuffer;
        }
        frameInfo.memoryPool->EndSet();

        std::vector<legit::ImageSamplerBinding> imageSamplerBindings;
        imageSamplerBindings.push_back(shaderDataSetInfo->MakeImageSamplerBinding("specularCubemap", this->specularCubemapView.get(), this->cubemapSampler.get()));
        auto dDensityDataView = passContext.GetImageView(sceneResources->dDensityProxy.mippedImageViewProxy->Id());
        imageSamplerBindings.push_back(shaderDataSetInfo->MakeImageSamplerBinding("dDensitySampler", dDensityDataView, this->volumeSampler.get()));

        std::vector<legit::StorageImageBinding> storageImageBindings;
        auto dIndirectLightView = passContext.GetImageView(sceneResources->dIndirectLightProxy.mips[0].imageViewProxy->Id());
        storageImageBindings.push_back(shaderDataSetInfo->MakeStorageImageBinding("dIndirectLightImage", dIndirectLightView));

        auto shaderDataSetBindings = legit::DescriptorSetBindings()
          .SetUniformBufferBindings(shaderDataSetUniforms.uniformBufferBindings)
          .SetImageSamplerBindings(imageSamplerBindings)
          .SetStorageImageBindings(storageImageBindings);

        auto shaderDataSet = this->core->GetDescriptorSetCache()->GetDescriptorSet(*shaderDataSetInfo, shaderDataSetBindings);
        passContext.GetCommandBuffer().bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipeineInfo.pipelineLayout, ShaderDataSetIndex, { shaderDataSet }, { shaderDataSetUniforms.dynamicOffset });

        glm::uvec3 workGroupSize = shader->GetLocalSize();
        glm::uvec3 cellSliceSize = sceneResources->volumeResolution / glm::uvec3(volumeDataBuffer.patternSize);


        passContext.GetCommandBuffer().dispatch(
          uint32_t(cellSliceSize.x / workGroupSize.x),
          uint32_t(cellSliceSize.y / workGroupSize.y),
          uint32_t(volumeDataBuffer.patternSize.x * volumeDataBuffer.patternSize.y * volumeDataBuffer.patternSize.z));
      }
    }));
  }

  /*void TestRun(const legit::InFlightQueue::FrameInfo &frameInfo, VolumeDataBuffer volumeDataBuffer)
  {
    core->GetRenderGraph()->AddPass(legit::RenderGraph::ComputePassDesc()
      .SetStorageImages(
      {
        sceneResources->dIndirectLightProxy.mips[0].imageViewProxy->Id()
      })
      .SetProfilerInfo(legit::Colors::clouds, "Perf test")
      .SetRecordFunc([this, frameInfo, volumeDataBuffer](legit::RenderGraph::PassContext passContext)
    {
      auto shader = this->indirectLightShader.compute.get();
      auto pipeineInfo = this->core->GetPipelineCache()->BindComputePipeline(passContext.GetCommandBuffer(), shader);
      {
        const legit::DescriptorSetLayoutKey *shaderDataSetInfo = shader->GetSetInfo(ShaderDataSetIndex);


        std::vector<legit::StorageImageBinding> storageImageBindings;
        auto indirectLightPreciseDataView = passContext.GetImageView(sceneResources->dIndirectLightProxy.mips[0].imageViewProxy->Id());
        storageImageBindings.push_back(shaderDataSetInfo->MakeStorageImageBinding("indirectLightPreciseImage", indirectLightPreciseDataView));

        auto shaderDataSetBindings = legit::DescriptorSetBindings()
          .SetStorageImageBindings(storageImageBindings);

        auto shaderDataSet = this->core->GetDescriptorSetCache()->GetDescriptorSet(*shaderDataSetInfo, shaderDataSetBindings);
        passContext.GetCommandBuffer().bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipeineInfo.pipelineLayout, ShaderDataSetIndex, { shaderDataSet }, { });

        glm::uvec3 workGroupSize = shader->GetLocalSize();
        passContext.GetCommandBuffer().dispatch(
          uint32_t(sceneResources->volumeResolution.x / workGroupSize.x), //because of checkerboard
          uint32_t(sceneResources->volumeResolution.y / workGroupSize.y),
          uint32_t(sceneResources->volumeResolution.z / workGroupSize.z));
      }
    }));
  }*/

  void DeinterleaveDensity(const legit::InFlightQueue::FrameInfo &frameInfo, VolumeDataBuffer volumeDataBuffer)
  {
    core->GetRenderGraph()->AddPass(legit::RenderGraph::ComputePassDesc()
      .SetInputImages({ sceneResources->iDensityProxy.mips[0].imageViewProxy->Id() })
      .SetStorageImages({ sceneResources->dDensityProxy.mips[0].imageViewProxy->Id() })
      .SetProfilerInfo(legit::Colors::clouds, "Deinterleave density")
      .SetRecordFunc([this, frameInfo, volumeDataBuffer](legit::RenderGraph::PassContext passContext)
    {
      auto shader = this->densityDeinterleaveShader.compute.get();
      auto pipeineInfo = this->core->GetPipelineCache()->BindComputePipeline(passContext.GetCommandBuffer(), shader);
      {
        const legit::DescriptorSetLayoutKey *shaderDataSetInfo = shader->GetSetInfo(ShaderDataSetIndex);
        auto shaderDataSetUniforms = frameInfo.memoryPool->BeginSet(shaderDataSetInfo);
        {
          auto mappedVolumeDataBuffer= frameInfo.memoryPool->GetUniformBufferData<VolumeDataBuffer>("VolumeDataBuffer");
          *mappedVolumeDataBuffer = volumeDataBuffer;
        }
        frameInfo.memoryPool->EndSet();

        std::vector<legit::ImageSamplerBinding> imageSamplerBindings;
        auto iDensityDataView = passContext.GetImageView(sceneResources->iDensityProxy.mips[0].imageViewProxy->Id());
        imageSamplerBindings.push_back(shaderDataSetInfo->MakeImageSamplerBinding("iDensitySampler", iDensityDataView, volumeSampler.get()));

        std::vector<legit::StorageImageBinding> storageImageBindings;
        auto dDensityDataView = passContext.GetImageView(sceneResources->dDensityProxy.mips[0].imageViewProxy->Id());
        storageImageBindings.push_back(shaderDataSetInfo->MakeStorageImageBinding("dDensityImage", dDensityDataView));

        auto shaderDataSetBindings = legit::DescriptorSetBindings()
          .SetUniformBufferBindings(shaderDataSetUniforms.uniformBufferBindings)
          .SetImageSamplerBindings(imageSamplerBindings)
          .SetStorageImageBindings(storageImageBindings);

        auto shaderDataSet = this->core->GetDescriptorSetCache()->GetDescriptorSet(*shaderDataSetInfo, shaderDataSetBindings);
        passContext.GetCommandBuffer().bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipeineInfo.pipelineLayout, ShaderDataSetIndex, { shaderDataSet }, { shaderDataSetUniforms.dynamicOffset });

        glm::uvec3 workGroupSize = shader->GetLocalSize();
        passContext.GetCommandBuffer().dispatch(
          uint32_t(sceneResources->volumeResolution.x / workGroupSize.x), //because of checkerboard
          uint32_t(sceneResources->volumeResolution.y / workGroupSize.y),
          uint32_t(sceneResources->volumeResolution.z / workGroupSize.z));
      }
    }));
  }

  void InterleaveIndirectLight(const legit::InFlightQueue::FrameInfo &frameInfo, VolumeDataBuffer volumeDataBuffer)
  {
    core->GetRenderGraph()->AddPass(legit::RenderGraph::ComputePassDesc()
      .SetInputImages({ sceneResources->dIndirectLightProxy.mips[0].imageViewProxy->Id() })
      .SetStorageImages({ sceneResources->iIndirectLightProxy.mips[0].imageViewProxy->Id() })
      .SetProfilerInfo(legit::Colors::clouds, "Interleave")
      .SetRecordFunc([this, frameInfo, volumeDataBuffer](legit::RenderGraph::PassContext passContext)
    {
      auto shader = this->indirectLightInterleaveShader.compute.get();
      auto pipeineInfo = this->core->GetPipelineCache()->BindComputePipeline(passContext.GetCommandBuffer(), shader);
      {
        const legit::DescriptorSetLayoutKey *shaderDataSetInfo = shader->GetSetInfo(ShaderDataSetIndex);
        auto shaderDataSetUniforms = frameInfo.memoryPool->BeginSet(shaderDataSetInfo);
        {
          auto mappedVolumeDataBuffer= frameInfo.memoryPool->GetUniformBufferData<VolumeDataBuffer>("VolumeDataBuffer");
          *mappedVolumeDataBuffer = volumeDataBuffer;
        }
        frameInfo.memoryPool->EndSet();

        std::vector<legit::ImageSamplerBinding> imageSamplerBindings;
        auto dIndirectLightDataView = passContext.GetImageView(sceneResources->dIndirectLightProxy.mips[0].imageViewProxy->Id());
        imageSamplerBindings.push_back(shaderDataSetInfo->MakeImageSamplerBinding("dIndirectLightSampler", dIndirectLightDataView, volumeSampler.get()));

        std::vector<legit::StorageImageBinding> storageImageBindings;
        auto iIndirectLightDataView = passContext.GetImageView(sceneResources->iIndirectLightProxy.mips[0].imageViewProxy->Id());
        storageImageBindings.push_back(shaderDataSetInfo->MakeStorageImageBinding("iIndirectLightImage", iIndirectLightDataView));

        auto shaderDataSetBindings = legit::DescriptorSetBindings()
          .SetUniformBufferBindings(shaderDataSetUniforms.uniformBufferBindings)
          .SetImageSamplerBindings(imageSamplerBindings)
          .SetStorageImageBindings(storageImageBindings);

        auto shaderDataSet = this->core->GetDescriptorSetCache()->GetDescriptorSet(*shaderDataSetInfo, shaderDataSetBindings);
        passContext.GetCommandBuffer().bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipeineInfo.pipelineLayout, ShaderDataSetIndex, { shaderDataSet }, { shaderDataSetUniforms.dynamicOffset });

        glm::uvec3 workGroupSize = shader->GetLocalSize();
        passContext.GetCommandBuffer().dispatch(
          uint32_t(sceneResources->volumeResolution.x / workGroupSize.x), //because of checkerboard
          uint32_t(sceneResources->volumeResolution.y / workGroupSize.y),
          uint32_t(sceneResources->volumeResolution.z / workGroupSize.z));
      }
    }));
  }
  void AddTransitionPass(legit::RenderGraph::ImageViewProxyId velocityProxyId, legit::RenderGraph::ImageViewProxyId waveFuncProxyId)
  {
    core->GetRenderGraph()->AddPass(legit::RenderGraph::ComputePassDesc()
      .SetStorageImages(
        {
          sceneResources->iDensityProxy.mippedImageViewProxy->Id(),
          sceneResources->dDensityProxy.mippedImageViewProxy->Id(),
          sceneResources->iIndirectLightProxy.mippedImageViewProxy->Id(),
          sceneResources->dIndirectLightProxy.mippedImageViewProxy->Id(),
          sceneResources->stepMapProxy.mippedImageViewProxy->Id(),
          velocityProxyId,
          waveFuncProxyId
        })
      .SetProfilerInfo(legit::Colors::clouds, "Image transition pass"));
  }

  struct SceneResources
  {
    SceneResources(legit::Core *core, glm::uvec3 _volumeResolution, glm::vec3 _aabbMin, glm::vec3 _aabbMax) :
      iDensityProxy(core, vk::Format::eR8G8B8A8Unorm, _volumeResolution, vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage, legit::ImageUsageTypes::ComputeShaderReadWrite),
      dDensityProxy(core, vk::Format::eR8G8B8A8Unorm, _volumeResolution, vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage, legit::ImageUsageTypes::ComputeShaderReadWrite),
      directLightProxy(core, vk::Format::eR8G8B8A8Unorm, _volumeResolution, vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage, legit::ImageUsageTypes::ComputeShaderReadWrite),
      iIndirectLightProxy(core, vk::Format::eR8G8B8A8Unorm, _volumeResolution, vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage, legit::ImageUsageTypes::ComputeShaderReadWrite),
      dIndirectLightProxy(core, vk::Format::eR32G32B32A32Sfloat, _volumeResolution, vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage, legit::ImageUsageTypes::ComputeShaderReadWrite),
      stepMapProxy(core, vk::Format::eR8G8B8A8Unorm, _volumeResolution, vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage, legit::ImageUsageTypes::ComputeShaderReadWrite)
    {
      volumeResolution = _volumeResolution;
      aabbMin = _aabbMin;
      aabbMax = _aabbMax;
      core->SetDebugName(iDensityProxy.volumeImage->GetImageData(), "I Density image");
      core->SetDebugName(dDensityProxy.volumeImage->GetImageData(), "D Density image");
      core->SetDebugName(directLightProxy.volumeImage->GetImageData(), "Direct light image");
      core->SetDebugName(iIndirectLightProxy.volumeImage->GetImageData(), "I Indirect light image");
      core->SetDebugName(dIndirectLightProxy.volumeImage->GetImageData(), "D Indirect light image");
      core->SetDebugName(stepMapProxy.volumeImage->GetImageData(), "Step map image");
    }
    PersistentMippedVolumeProxy iDensityProxy;
    PersistentMippedVolumeProxy dDensityProxy;
    PersistentMippedVolumeProxy directLightProxy;
    PersistentMippedVolumeProxy iIndirectLightProxy;
    PersistentMippedVolumeProxy dIndirectLightProxy;
    PersistentMippedVolumeProxy stepMapProxy;
    glm::uvec3 volumeResolution;
    glm::vec3 aabbMin, aabbMax;
  };

  std::unique_ptr<SceneResources> sceneResources;

  struct ViewportResources
  {
    ViewportResources(vk::Extent2D _extent) :
      extent(_extent)
    {
    }
    vk::Extent2D extent;
  };
  std::unique_ptr<ViewportResources> viewportResources;

  struct VolumeRendererShader
  {
    std::unique_ptr<legit::Shader> vertex;
    std::unique_ptr<legit::Shader> fragment;
    std::unique_ptr<legit::ShaderProgram> program;
  } volumeRendererShader;

  struct VolumeInitShader
  {
    std::unique_ptr<legit::Shader> compute;
  } volumesInitShader;

  struct VolumeLoadShader
  {
    std::unique_ptr<legit::Shader> compute;
  } volumeLoadShader;

  struct StepMapShader
  {
    std::unique_ptr<legit::Shader> compute;
  } stepMapShader;

  struct IndirectLightShader
  {
    std::unique_ptr<legit::Shader> compute;
  } indirectLightShader;

  struct VolumeSweeperShader
  {
    std::unique_ptr<legit::Shader> compute;
  } volumeSweeperShader;

  struct DensityDeinterleaveShader
  {
    std::unique_ptr<legit::Shader> compute;
  } densityDeinterleaveShader;

  struct IndirectLightInterleaveShader
  {
    std::unique_ptr<legit::Shader> compute;
  } indirectLightInterleaveShader;

  std::unique_ptr<legit::Sampler> cubemapSampler;
  std::unique_ptr<legit::Sampler> volumeSampler;

  ShrodingerSolver shrodingerSolver;

  std::unique_ptr<legit::Image> specularCubemap;
  std::unique_ptr<legit::ImageView> specularCubemapView;
  std::unique_ptr<legit::Image> diffuseCubemap;
  std::unique_ptr<legit::ImageView> diffuseCubemapView;

  MipBuilder mipBuilder;

  std::default_random_engine eng;
  std::uniform_real_distribution<float> dis{ 0.0f, 1.0f };

  legit::Core *core;
};