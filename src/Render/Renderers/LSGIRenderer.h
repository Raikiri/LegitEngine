#include "../Common/MipBuilder.h"
#include "../Common/BlurBuilder.h"
#include "../Common/DebugRenderer.h"
#include "../Common/InterleaveBuilder.h"

class LSGIRenderer
{
public:
  LSGIRenderer(legit::Core *_core) :
    mipBuilder(_core),
    blurBuilder(_core),
    interleaveBuilder(_core),
    debugRenderer(_core)
  {
    this->core = _core;

    vertexDecl = Mesh::GetVertexDeclaration();

    screenspaceSampler.reset(new legit::Sampler(core->GetLogicalDevice(), vk::SamplerAddressMode::eClampToEdge, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear));
    shadowmapSampler.reset(new legit::Sampler(core->GetLogicalDevice(), vk::SamplerAddressMode::eClampToEdge, vk::Filter::eLinear, vk::SamplerMipmapMode::eNearest, true));
    ReloadShaders();
  }
  void RecreateSceneResources(Scene *scene)
  {
  }
  void RecreateSwapchainResources(vk::Extent2D viewportExtent, size_t inFlightFramesCount)
  {
    this->viewportExtent = viewportExtent;
    glm::uvec2 viewportSize = { viewportExtent.width, viewportExtent.height };
    viewportResources.reset(new ViewportResources(core->GetRenderGraph(), viewportSize));
  }
  struct PassData
  {
    legit::ShaderMemoryPool *memoryPool;
    //legit::RenderGraph::ImageViewProxyId swapchainImageViewProxyId;
    glm::mat4 viewMatrix;
    glm::mat4 projMatrix;
    glm::mat4 lightViewMatrix;
    glm::mat4 lightProjMatrix;
    float time;
    Scene *scene;
  };

#pragma pack(push, 1)
  struct DrawCallDataBuffer
  {
    glm::mat4 modelMatrix;
    glm::vec4 albedoColor;
    glm::vec4 emissiveColor;
  };
#pragma pack(pop)

  void RenderFrame(const legit::InFlightQueue::FrameInfo &frameInfo, const Camera &camera, const Camera &light, Scene *scene, GLFWwindow *window)
  {
    static float time = 0.0f;
    time += 0.01f;


    PassData passData;

    passData.memoryPool = frameInfo.memoryPool;
    passData.scene = scene;
    passData.viewMatrix = glm::inverse(camera.GetTransformMatrix());
    passData.lightViewMatrix = glm::inverse(light.GetTransformMatrix());
    passData.time = time;
    //passData.swapchainImageViewProxyId = frameInfo.swapchainImageViewProxyId;
    float aspect = float(this->viewportExtent.width) / float(this->viewportExtent.height);

    const glm::mat4 clip( //transform to match opengl
      1.0f, 0.0f, 0.0f, 0.0f,
      0.0f, -1.0f, 0.0f, 0.0f,
      0.0f, 0.0f, 0.5f, 0.0f,
      0.0f, 0.0f, 0.5f, 1.0f);

    passData.projMatrix = glm::perspective(1.0f, aspect, 0.01f, 1000.0f) * glm::scale(glm::vec3(1.0f, -1.0f, -1.0f));
    passData.lightProjMatrix = glm::perspective(0.8f, 1.0f, 0.1f, 100.0f) * glm::scale(glm::vec3(1.0f, -1.0f, -1.0f));

    RenderShadowMap(passData);
    RenderGBuffer(passData);
    RenderDirectLighting(passData);


    interleaveBuilder.Deinterleave(core->GetRenderGraph(), frameInfo.memoryPool, this->viewportResources->normal.imageViewProxy->Id(), this->viewportResources->dNormal.imageViewProxy->Id(), glm::uvec2(4, 4));
    interleaveBuilder.Deinterleave(core->GetRenderGraph(), frameInfo.memoryPool, this->viewportResources->depthStencil.imageViewProxy->Id(), this->viewportResources->dDepthStencil.imageViewProxy->Id(), glm::uvec2(4, 4));
    interleaveBuilder.Deinterleave(core->GetRenderGraph(), frameInfo.memoryPool, this->viewportResources->directLight.mipImageViewProxies[0]->Id(), this->viewportResources->dDirectLight.imageViewProxy->Id(), glm::uvec2(4, 4));


    ClearLineSweeper(passData);
    //ComputeSectorGI(passData);
    PrecompSweepData(passData);
    ComputeRayGI(passData);

    RenderFinalGathering(frameInfo.swapchainImageViewProxyId, passData);
    //final gathering
    

    std::vector<legit::RenderGraph::ImageViewProxyId> debugProxies;
    //debugProxies = this->viewportResources->blurredDirectLight.mipImageViewProxies;
    //debugProxies.push_back(this->viewportResources->indirectLight.imageViewProxy->Id());

    debugProxies.push_back(this->viewportResources->dNormal.imageViewProxy->Id());
    debugProxies.push_back(this->viewportResources->dDepthStencil.imageViewProxy->Id());
    debugProxies.push_back(this->viewportResources->dDirectLight.imageViewProxy->Id());
    debugProxies.push_back(this->viewportResources->dRayDepth.imageViewProxy->Id());

    debugRenderer.RenderImageViews(core->GetRenderGraph(), frameInfo.memoryPool, frameInfo.swapchainImageViewProxyId, debugProxies);
  }

  void RenderShadowMap(PassData passData)
  {
    //rendering shadow map
    vk::Extent2D shadowMapExtent(this->viewportResources->shadowMap.baseSize.x, this->viewportResources->shadowMap.baseSize.y);
    core->GetRenderGraph()->AddRenderPass(
      {
      }, this->viewportResources->shadowMap.imageViewProxy->Id(),
      {}, shadowMapExtent, vk::AttachmentLoadOp::eClear, [this, passData](legit::RenderGraph::RenderPassContext passContext)
    {
      auto pipeineInfo = this->core->GetPipelineCache()->BindGraphicsPipeline(passContext.GetCommandBuffer(), passContext.GetRenderPass()->GetHandle(), legit::DepthSettings::DepthTest(), { legit::BlendSettings::Opaque() }, vertexDecl, vk::PrimitiveTopology::eTriangleList, shadowmapBuilderShader.program.get());
      {
        const legit::DescriptorSetLayoutKey *shaderDataSetInfo = shadowmapBuilderShader.vertex->GetSetInfo(ShaderDataSetIndex);
        auto shaderData = passData.memoryPool->BeginSet(shaderDataSetInfo);
        {
          auto shaderDataBuffer = passData.memoryPool->GetUniformBufferData<ShadowmapBuilderShader::DataBuffer>("ShadowmapBuilderData");

          shaderDataBuffer->lightViewMatrix = passData.lightViewMatrix;
          shaderDataBuffer->lightProjMatrix = passData.lightProjMatrix;
        }
        passData.memoryPool->EndSet();
        auto shaderDataSet = this->core->GetDescriptorSetCache()->GetDescriptorSet(*shaderDataSetInfo, shaderData.uniformBufferBindings, {}, {});

        const legit::DescriptorSetLayoutKey *drawCallSetInfo = shadowmapBuilderShader.vertex->GetSetInfo(DrawCallDataSetIndex);

        passData.scene->IterateObjects([&](glm::mat4 objectToWorld, glm::vec3 albedoColor, glm::vec3 emissiveColor, vk::Buffer vertexBuffer, vk::Buffer indexBuffer, uint32_t verticesCount, uint32_t indicesCount)
        {
          auto drawCallData = passData.memoryPool->BeginSet(drawCallSetInfo);
          {
            auto drawCallData = passData.memoryPool->GetUniformBufferData<DrawCallDataBuffer>("DrawCallData");
            drawCallData->modelMatrix = objectToWorld;
            drawCallData->albedoColor = glm::vec4(albedoColor, 1.0f);
            drawCallData->emissiveColor = glm::vec4(emissiveColor, 1.0f);
          }
          passData.memoryPool->EndSet();
          auto drawCallSet = this->core->GetDescriptorSetCache()->GetDescriptorSet(*drawCallSetInfo, drawCallData.uniformBufferBindings, {}, {});
          passContext.GetCommandBuffer().bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeineInfo.pipelineLayout, ShaderDataSetIndex,
            { shaderDataSet, drawCallSet },
            { shaderData.dynamicOffset, drawCallData.dynamicOffset });

          passContext.GetCommandBuffer().bindVertexBuffers(0, { vertexBuffer }, { 0 });
          passContext.GetCommandBuffer().bindIndexBuffer(indexBuffer, 0, vk::IndexType::eUint32);
          passContext.GetCommandBuffer().drawIndexed(indicesCount, 1, 0, 0, 0);
        });
      }
    });
  }

  void RenderGBuffer(PassData passData)
  {
    //rendering gbuffer
    core->GetRenderGraph()->AddRenderPass(
      {
        this->viewportResources->albedo.imageViewProxy->Id(),       //location = 0
        this->viewportResources->emissive.imageViewProxy->Id(),     //location = 1
        this->viewportResources->normal.imageViewProxy->Id(),       //location = 2
        this->viewportResources->depthMoments.mipImageViewProxies[0]->Id(), //location = 3
      }, this->viewportResources->depthStencil.imageViewProxy->Id(),
      {}, this->viewportExtent, vk::AttachmentLoadOp::eClear, [this, passData](legit::RenderGraph::RenderPassContext passContext)
    {
      std::vector<legit::BlendSettings> attachmentBlendSettings;
      attachmentBlendSettings.resize(passContext.GetRenderPass()->GetColorAttachmentsCount(), legit::BlendSettings::Opaque());
      auto pipeineInfo = this->core->GetPipelineCache()->BindGraphicsPipeline(passContext.GetCommandBuffer(), passContext.GetRenderPass()->GetHandle(), legit::DepthSettings::DepthTest(), attachmentBlendSettings, vertexDecl, vk::PrimitiveTopology::eTriangleList, gBufferBuilderShader.program.get());
      {
        const legit::DescriptorSetLayoutKey *shaderDataSetInfo = gBufferBuilderShader.vertex->GetSetInfo(ShaderDataSetIndex);
        auto shaderData = passData.memoryPool->BeginSet(shaderDataSetInfo);
        {
          auto shaderDataBuffer = passData.memoryPool->GetUniformBufferData<GBufferBuilderShader::DataBuffer>("GBufferBuilderData");

          shaderDataBuffer->time = 0.0f;
          shaderDataBuffer->projMatrix = passData.projMatrix;
          shaderDataBuffer->viewMatrix = passData.viewMatrix;
        }
        passData.memoryPool->EndSet();
        auto shaderDataSet = this->core->GetDescriptorSetCache()->GetDescriptorSet(*shaderDataSetInfo, shaderData.uniformBufferBindings, {}, {});

        const legit::DescriptorSetLayoutKey *drawCallSetInfo = gBufferBuilderShader.vertex->GetSetInfo(DrawCallDataSetIndex);

        passData.scene->IterateObjects([&](glm::mat4 objectToWorld, glm::vec3 albedoColor, glm::vec3 emissiveColor, vk::Buffer vertexBuffer, vk::Buffer indexBuffer, uint32_t verticesCount, uint32_t indicesCount)
        {
          auto drawCallData = passData.memoryPool->BeginSet(drawCallSetInfo);
          {
            auto drawCallData = passData.memoryPool->GetUniformBufferData<DrawCallDataBuffer>("DrawCallData");
            drawCallData->modelMatrix = objectToWorld;
            drawCallData->albedoColor = glm::vec4(albedoColor, 1.0f);
            drawCallData->emissiveColor = glm::vec4(emissiveColor, 1.0f);
          }
          passData.memoryPool->EndSet();
          auto drawCallSet = this->core->GetDescriptorSetCache()->GetDescriptorSet(*drawCallSetInfo, drawCallData.uniformBufferBindings, {}, {});
          passContext.GetCommandBuffer().bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeineInfo.pipelineLayout, ShaderDataSetIndex,
            { shaderDataSet, drawCallSet },
            { shaderData.dynamicOffset, drawCallData.dynamicOffset });

          passContext.GetCommandBuffer().bindVertexBuffers(0, { vertexBuffer }, { 0 });
          passContext.GetCommandBuffer().bindIndexBuffer(indexBuffer, 0, vk::IndexType::eUint32);
          passContext.GetCommandBuffer().drawIndexed(indicesCount, 1, 0, 0, 0);
        });
      }
    });
  }

  void RenderDirectLighting(PassData passData)
  {
    core->GetRenderGraph()->AddRenderPass(
      {
        this->viewportResources->directLight.mipImageViewProxies[0]->Id(),       //render targets
      }, legit::RenderGraph::ImageViewProxyId(), //depth
      {
        this->viewportResources->albedo.imageViewProxy->Id(), //read resources
        this->viewportResources->emissive.imageViewProxy->Id(),
        this->viewportResources->normal.imageViewProxy->Id(),
        this->viewportResources->depthStencil.imageViewProxy->Id(),
        this->viewportResources->shadowMap.imageViewProxy->Id(),
      },
      this->viewportExtent, vk::AttachmentLoadOp::eDontCare, [this, passData](legit::RenderGraph::RenderPassContext passContext)
    {
      //binding pipeline: blend mode, shader, etc
      auto pipeineInfo = this->core->GetPipelineCache()->BindGraphicsPipeline(
        passContext.GetCommandBuffer(), passContext.GetRenderPass()->GetHandle(), legit::DepthSettings::Disabled(), { legit::BlendSettings::Opaque() }, legit::VertexDeclaration(), vk::PrimitiveTopology::eTriangleFan, directLightingShader.program.get());
      {
        //shaderDataSetInfo is a descriptor set that has ShaderDataSetIndex 
        const legit::DescriptorSetLayoutKey *shaderDataSetInfo = directLightingShader.fragment->GetSetInfo(ShaderDataSetIndex);
        //this creates an array of bindings (buffers + offsets) for all uniform buffers in this descriptor set
        auto shaderData = passData.memoryPool->BeginSet(shaderDataSetInfo);
        {
          //returns pointer to mapped uniform buffer named "DirectLightingData"
          auto shaderDataBuffer = passData.memoryPool->GetUniformBufferData<DirectLightingShader::DataBuffer>("DirectLightingData");

          shaderDataBuffer->viewMatrix = passData.viewMatrix;
          shaderDataBuffer->projMatrix = passData.projMatrix;
          shaderDataBuffer->lightViewMatrix = passData.lightViewMatrix;
          shaderDataBuffer->lightProjMatrix = passData.lightProjMatrix;
          shaderDataBuffer->time = 0.0f;
        }
        passData.memoryPool->EndSet();

        std::vector<legit::ImageSamplerBinding> imageSamplerBindings;
        //proxies are virtual handles to temporary images/views. this code resolves a proxy into actual handle.
        auto albedoImageView = passContext.GetImageView(this->viewportResources->albedo.imageViewProxy->Id());
        //every imagesampler binding binds an image view and a sampler to a named binding point in shader
        imageSamplerBindings.push_back(shaderDataSetInfo->MakeImageSamplerBinding("albedoSampler", albedoImageView, screenspaceSampler.get()));
        auto emissiveImageView = passContext.GetImageView(this->viewportResources->emissive.imageViewProxy->Id());
        imageSamplerBindings.push_back(shaderDataSetInfo->MakeImageSamplerBinding("emissiveSampler", emissiveImageView, screenspaceSampler.get()));
        auto normalImageView = passContext.GetImageView(this->viewportResources->normal.imageViewProxy->Id());
        imageSamplerBindings.push_back(shaderDataSetInfo->MakeImageSamplerBinding("normalSampler", normalImageView, screenspaceSampler.get()));
        auto depthStencilImageView = passContext.GetImageView(this->viewportResources->depthStencil.imageViewProxy->Id());
        imageSamplerBindings.push_back(shaderDataSetInfo->MakeImageSamplerBinding("depthStencilSampler", depthStencilImageView, screenspaceSampler.get()));
        auto shadowmapImageView = passContext.GetImageView(this->viewportResources->shadowMap.imageViewProxy->Id());
        imageSamplerBindings.push_back(shaderDataSetInfo->MakeImageSamplerBinding("shadowmapSampler", shadowmapImageView, shadowmapSampler.get()));

        auto shaderDataSet = this->core->GetDescriptorSetCache()->GetDescriptorSet(*shaderDataSetInfo, shaderData.uniformBufferBindings, {}, imageSamplerBindings);

        passContext.GetCommandBuffer().bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeineInfo.pipelineLayout, ShaderDataSetIndex, { shaderDataSet }, { shaderData.dynamicOffset });
        passContext.GetCommandBuffer().draw(4, 1, 0, 0);
      }
    });
  }

  void ClearLineSweeper(PassData passData)
  {
    core->GetRenderGraph()->AddPass(legit::RenderGraph::ComputePassDesc()
      .SetStorageImages(
      {
        this->viewportResources->indirectLight.imageViewProxy->Id(),
        this->viewportResources->lineSweepStackImage.imageViewProxy->Id()
      })
      .SetStorageBuffers(
      {
        this->viewportResources->lineSweepStackBuf->Id()
      })
      .SetRecordFunc([this, passData](legit::RenderGraph::PassContext passContext)
    {
      auto shader = clearShader.compute.get();

      auto pipeineInfo = this->core->GetPipelineCache()->BindComputePipeline(passContext.GetCommandBuffer(), shader);
      {
        const legit::DescriptorSetLayoutKey *shaderDataSetInfo = clearShader.compute->GetSetInfo(ShaderDataSetIndex);
        auto shaderData = passData.memoryPool->BeginSet(shaderDataSetInfo);
        {
          auto shaderDataBuffer = passData.memoryPool->GetUniformBufferData<ClearShader::DataBuffer>("ClearShaderData");

          shaderDataBuffer->viewportSize = glm::vec4(this->viewportExtent.width, this->viewportExtent.height, 0.0f, 0.0f);
        }
        passData.memoryPool->EndSet();

        std::vector<legit::StorageImageBinding> storageImageBindings;
        auto indirectLight = passContext.GetImageView(this->viewportResources->indirectLight.imageViewProxy->Id());
        storageImageBindings.push_back(shaderDataSetInfo->MakeStorageImageBinding("indirectLight", indirectLight));
        /*auto stackImageView = passContext.GetImageView(this->viewportResources->lineSweepStackImage.imageViewProxy->Id());
        storageImageBindings.push_back(shaderDataSetInfo->MakeStorageImageBinding("sweepStackImage", stackImageView));*/

        auto shaderDataSetBindings = legit::DescriptorSetBindings()
          .SetUniformBufferBindings(shaderData.uniformBufferBindings)
          .SetStorageImageBindings(storageImageBindings);
        auto shaderDataSet = this->core->GetDescriptorSetCache()->GetDescriptorSet(*shaderDataSetInfo, shaderDataSetBindings);

        passContext.GetCommandBuffer().bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipeineInfo.pipelineLayout, ShaderDataSetIndex, { shaderDataSet }, { shaderData.dynamicOffset });
        glm::uvec3 workGroupSize = shader->GetLocalSize();
        passContext.GetCommandBuffer().dispatch(uint32_t(this->viewportExtent.width / workGroupSize.x), uint32_t(this->viewportExtent.height / workGroupSize.y), 1);
      }
    }));
  }
  void ComputeSectorGI(PassData passData)
  {
    int batchesCount = 1;
    for(int batchIndex = 0; batchIndex < batchesCount; batchIndex++)
    {
      core->GetRenderGraph()->AddPass(legit::RenderGraph::ComputePassDesc()
        .SetStorageBuffers(
        {
          this->viewportResources->lineSweepStackBuf->Id()
        })
        .SetStorageImages(
        {
          this->viewportResources->indirectLight.imageViewProxy->Id()
        })
        .SetInputImages(
        {
          this->viewportResources->dNormal.imageViewProxy->Id(),
          this->viewportResources->dDepthStencil.imageViewProxy->Id(),
          this->viewportResources->dDirectLight.imageViewProxy->Id()
        })
        .SetRecordFunc([this, passData, batchesCount, batchIndex](legit::RenderGraph::PassContext passContext)
      {
        auto shader = indirectLightingShader.compute.get();
        auto pipeineInfo = this->core->GetPipelineCache()->BindComputePipeline(passContext.GetCommandBuffer(), shader);
        {
          const legit::DescriptorSetLayoutKey *shaderDataSetInfo = shader->GetSetInfo(ShaderDataSetIndex);
          auto shaderData = passData.memoryPool->BeginSet(shaderDataSetInfo);
          {
            auto shaderDataBuffer = passData.memoryPool->GetUniformBufferData<IndirectLightingShader::DataBuffer>("IndirectLightingData");

            shaderDataBuffer->viewMatrix = passData.viewMatrix;
            shaderDataBuffer->projMatrix = passData.projMatrix;
            shaderDataBuffer->viewportSize = glm::vec4(this->viewportExtent.width, this->viewportExtent.height, 0.0f, 0.0f);
            shaderDataBuffer->time = passData.time;
            shaderDataBuffer->batchesCount = batchesCount;
            shaderDataBuffer->batchIndex = batchIndex;
          }
          passData.memoryPool->EndSet();

          std::vector<legit::ImageSamplerBinding> imageSamplerBindings;
          auto directLightView = passContext.GetImageView(this->viewportResources->dDirectLight.imageViewProxy->Id());
          imageSamplerBindings.push_back(shaderDataSetInfo->MakeImageSamplerBinding("dDirectLightSampler", directLightView, screenspaceSampler.get()));
          auto normalImageView = passContext.GetImageView(this->viewportResources->dNormal.imageViewProxy->Id());
          imageSamplerBindings.push_back(shaderDataSetInfo->MakeImageSamplerBinding("dNormalSampler", normalImageView, screenspaceSampler.get()));
          auto depthStencilImageView = passContext.GetImageView(this->viewportResources->dDepthStencil.imageViewProxy->Id());
          imageSamplerBindings.push_back(shaderDataSetInfo->MakeImageSamplerBinding("dDepthStencilSampler", depthStencilImageView, screenspaceSampler.get()));

          std::vector<legit::StorageImageBinding> storageImageBindings;
          auto indirectLightView = passContext.GetImageView(this->viewportResources->indirectLight.imageViewProxy->Id());
          storageImageBindings.push_back(shaderDataSetInfo->MakeStorageImageBinding("indirectLight", indirectLightView));

          std::vector<legit::StorageBufferBinding> storageBufferBindings;
          auto lineSweepStack = passContext.GetBuffer(this->viewportResources->lineSweepStackBuf->Id());
          storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("LineSweepStackData", lineSweepStack));

          auto shaderDataSetBindings = legit::DescriptorSetBindings()
            .SetUniformBufferBindings(shaderData.uniformBufferBindings)
            .SetStorageBufferBindings(storageBufferBindings)
            .SetStorageImageBindings(storageImageBindings)
            .SetImageSamplerBindings(imageSamplerBindings);

          auto shaderDataSet = this->core->GetDescriptorSetCache()->GetDescriptorSet(*shaderDataSetInfo, shaderDataSetBindings);

          passContext.GetCommandBuffer().bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipeineInfo.pipelineLayout, ShaderDataSetIndex, { shaderDataSet }, { shaderData.dynamicOffset });
          glm::uvec3 workGroupSize = shader->GetLocalSize();

          size_t gridSize = 4;
          size_t linesCount = this->viewportExtent.width / gridSize;
          passContext.GetCommandBuffer().dispatch(uint32_t(linesCount / workGroupSize.x), uint32_t(gridSize * gridSize), 1);
        }
      }));
    }
  }

  void PrecompSweepData(PassData passData)
  {
    core->GetRenderGraph()->AddPass(legit::RenderGraph::ComputePassDesc()
      .SetStorageBuffers(
      {
        this->viewportResources->lineSweepStackBuf->Id()
      })
      .SetStorageImages(
      {
          this->viewportResources->dRayDepth.imageViewProxy->Id()
      })
      .SetInputImages(
      {
        this->viewportResources->dNormal.imageViewProxy->Id(),
        this->viewportResources->dDepthStencil.imageViewProxy->Id(),
        this->viewportResources->dDirectLight.imageViewProxy->Id(),
      })
      .SetProfilerInfo(legit::Colors::peterRiver, "SweepPrecomp")
      .SetRecordFunc([this, passData](legit::RenderGraph::PassContext passContext)
    {
      auto shader = sweepPrecompShader.compute.get();
      auto pipeineInfo = this->core->GetPipelineCache()->BindComputePipeline(passContext.GetCommandBuffer(), shader);
      {
        const legit::DescriptorSetLayoutKey *shaderDataSetInfo = shader->GetSetInfo(ShaderDataSetIndex);
        auto shaderData = passData.memoryPool->BeginSet(shaderDataSetInfo);
        {
          auto shaderDataBuffer = passData.memoryPool->GetUniformBufferData<SweepDataBuffer>("SweepDataBuffer");

          shaderDataBuffer->viewMatrix = passData.viewMatrix;
          shaderDataBuffer->projMatrix = passData.projMatrix;
          shaderDataBuffer->viewportSize = glm::vec4(this->viewportExtent.width, this->viewportExtent.height, 0.0f, 0.0f);
          shaderDataBuffer->time = passData.time;
          shaderDataBuffer->batchesCount = 0;
          shaderDataBuffer->batchIndex = 0;
        }
        passData.memoryPool->EndSet();

        std::vector<legit::ImageSamplerBinding> imageSamplerBindings;
        auto directLightView = passContext.GetImageView(this->viewportResources->dDirectLight.imageViewProxy->Id());
        imageSamplerBindings.push_back(shaderDataSetInfo->MakeImageSamplerBinding("dDirectLightSampler", directLightView, screenspaceSampler.get()));
        auto normalImageView = passContext.GetImageView(this->viewportResources->dNormal.imageViewProxy->Id());
        imageSamplerBindings.push_back(shaderDataSetInfo->MakeImageSamplerBinding("dNormalSampler", normalImageView, screenspaceSampler.get()));
        auto depthStencilImageView = passContext.GetImageView(this->viewportResources->dDepthStencil.imageViewProxy->Id());
        imageSamplerBindings.push_back(shaderDataSetInfo->MakeImageSamplerBinding("dDepthStencilSampler", depthStencilImageView, screenspaceSampler.get()));

        std::vector<legit::StorageImageBinding> storageImageBindings;
        auto dRayDepthView = passContext.GetImageView(this->viewportResources->dRayDepth.imageViewProxy->Id());
        storageImageBindings.push_back(shaderDataSetInfo->MakeStorageImageBinding("dRayDepthImage", dRayDepthView));

        std::vector<legit::StorageBufferBinding> storageBufferBindings;
        /*auto lineSweepStack = passContext.GetBuffer(this->viewportResources->lineSweepStack->Id());
        storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("LineSweepStackData", lineSweepStack));*/

        auto shaderDataSetBindings = legit::DescriptorSetBindings()
          .SetUniformBufferBindings(shaderData.uniformBufferBindings)
          .SetStorageBufferBindings(storageBufferBindings)
          .SetStorageImageBindings(storageImageBindings)
          .SetImageSamplerBindings(imageSamplerBindings);

        auto shaderDataSet = this->core->GetDescriptorSetCache()->GetDescriptorSet(*shaderDataSetInfo, shaderDataSetBindings);

        passContext.GetCommandBuffer().bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipeineInfo.pipelineLayout, ShaderDataSetIndex, { shaderDataSet }, { shaderData.dynamicOffset });
        glm::uvec3 workGroupSize = shader->GetLocalSize();

        size_t gridSize = 4;
        size_t linesCount = this->viewportExtent.width / gridSize;
        passContext.GetCommandBuffer().dispatch(uint32_t(linesCount / workGroupSize.x), uint32_t(gridSize * gridSize), 1);
      }
    }));
  }
  void ComputeRayGI(PassData passData)
  {
    int batchesCount = 1;
    for(int batchIndex = 0; batchIndex < batchesCount; batchIndex++)
    {
      core->GetRenderGraph()->AddPass(legit::RenderGraph::ComputePassDesc()
        .SetStorageBuffers(
        {
          this->viewportResources->lineSweepStackBuf->Id()
        })
        .SetStorageImages(
        {
          this->viewportResources->indirectLight.imageViewProxy->Id(),
          this->viewportResources->lineSweepStackImage.imageViewProxy->Id()
        })
        .SetInputImages(
        {
          this->viewportResources->dNormal.imageViewProxy->Id(),
          this->viewportResources->dDepthStencil.imageViewProxy->Id(),
          this->viewportResources->dDirectLight.imageViewProxy->Id(),
          this->viewportResources->dRayDepth.imageViewProxy->Id()
        })
        .SetProfilerInfo(legit::Colors::pomegranate, "RayGI")
        .SetRecordFunc([this, passData, batchesCount, batchIndex](legit::RenderGraph::PassContext passContext)
      {
        auto shader = indirectRayShader.compute.get();
        auto pipeineInfo = this->core->GetPipelineCache()->BindComputePipeline(passContext.GetCommandBuffer(), shader);
        {
          const legit::DescriptorSetLayoutKey *shaderDataSetInfo = shader->GetSetInfo(ShaderDataSetIndex);
          auto shaderData = passData.memoryPool->BeginSet(shaderDataSetInfo);
          {
            auto shaderDataBuffer = passData.memoryPool->GetUniformBufferData<SweepDataBuffer>("SweepDataBuffer");

            shaderDataBuffer->viewMatrix = passData.viewMatrix;
            shaderDataBuffer->projMatrix = passData.projMatrix;
            shaderDataBuffer->viewportSize = glm::vec4(this->viewportExtent.width, this->viewportExtent.height, 0.0f, 0.0f);
            shaderDataBuffer->time = passData.time;
            shaderDataBuffer->batchesCount = batchesCount;
            shaderDataBuffer->batchIndex = batchIndex;
          }
          passData.memoryPool->EndSet();

          std::vector<legit::ImageSamplerBinding> imageSamplerBindings;
          auto directLightView = passContext.GetImageView(this->viewportResources->dDirectLight.imageViewProxy->Id());
          imageSamplerBindings.push_back(shaderDataSetInfo->MakeImageSamplerBinding("dDirectLightSampler", directLightView, screenspaceSampler.get()));
          auto normalImageView = passContext.GetImageView(this->viewportResources->dNormal.imageViewProxy->Id());
          imageSamplerBindings.push_back(shaderDataSetInfo->MakeImageSamplerBinding("dNormalSampler", normalImageView, screenspaceSampler.get()));
          auto depthStencilImageView = passContext.GetImageView(this->viewportResources->dDepthStencil.imageViewProxy->Id());
          imageSamplerBindings.push_back(shaderDataSetInfo->MakeImageSamplerBinding("dDepthStencilSampler", depthStencilImageView, screenspaceSampler.get()));
          auto rayDepthImageView = passContext.GetImageView(this->viewportResources->dRayDepth.imageViewProxy->Id());
          imageSamplerBindings.push_back(shaderDataSetInfo->MakeImageSamplerBinding("dRayDepthSampler", rayDepthImageView, screenspaceSampler.get()));
          

          std::vector<legit::StorageImageBinding> storageImageBindings;
          auto indirectLightView = passContext.GetImageView(this->viewportResources->indirectLight.imageViewProxy->Id());
          storageImageBindings.push_back(shaderDataSetInfo->MakeStorageImageBinding("dIndirectLightImage", indirectLightView));
          auto stackImageView = passContext.GetImageView(this->viewportResources->lineSweepStackImage.imageViewProxy->Id());
          storageImageBindings.push_back(shaderDataSetInfo->MakeStorageImageBinding("lineSweepStackImage", stackImageView));

          std::vector<legit::StorageBufferBinding> storageBufferBindings;
          auto lineSweepStack = passContext.GetBuffer(this->viewportResources->lineSweepStackBuf->Id());
          storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("LineSweepStackData", lineSweepStack));

          auto shaderDataSetBindings = legit::DescriptorSetBindings()
            .SetUniformBufferBindings(shaderData.uniformBufferBindings)
            .SetStorageBufferBindings(storageBufferBindings)
            .SetStorageImageBindings(storageImageBindings)
            .SetImageSamplerBindings(imageSamplerBindings);

          auto shaderDataSet = this->core->GetDescriptorSetCache()->GetDescriptorSet(*shaderDataSetInfo, shaderDataSetBindings);

          passContext.GetCommandBuffer().bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipeineInfo.pipelineLayout, ShaderDataSetIndex, { shaderDataSet }, { shaderData.dynamicOffset });
          glm::uvec3 workGroupSize = shader->GetLocalSize();

          size_t gridSize = 4;
          size_t linesCount = this->viewportExtent.width / gridSize;
          passContext.GetCommandBuffer().dispatch(uint32_t(linesCount / workGroupSize.x), uint32_t(gridSize * gridSize / workGroupSize.y), 1);
        }
      }));
    }
  }

  void RenderFinalGathering(legit::RenderGraph::ImageViewProxyId swapchainImageViewId, PassData passData)
  {
    core->GetRenderGraph()->AddPass(legit::RenderGraph::RenderPassDesc()
      .SetColorAttachments({ { swapchainImageViewId, vk::AttachmentLoadOp::eDontCare } })
      .SetInputImages(
      {
        this->viewportResources->directLight.imageViewProxy->Id(),
        this->viewportResources->albedo.imageViewProxy->Id(),
        this->viewportResources->indirectLight.imageViewProxy->Id(),
      })
      .SetStorageBuffers(
      {
        this->viewportResources->lineSweepStackBuf->Id()
      })
      .SetProfilerInfo(legit::Colors::pomegranate, "Gathering")
      .SetRenderAreaExtent(this->viewportExtent)
      .SetRecordFunc( [this, passData](legit::RenderGraph::RenderPassContext passContext)
    {
      auto pipeineInfo = this->core->GetPipelineCache()->BindGraphicsPipeline(passContext.GetCommandBuffer(), passContext.GetRenderPass()->GetHandle(), legit::DepthSettings::Disabled(), { legit::BlendSettings::Opaque() }, legit::VertexDeclaration(), vk::PrimitiveTopology::eTriangleFan, finalGathererShader.program.get());
      {
        const legit::DescriptorSetLayoutKey *shaderDataSetInfo = finalGathererShader.fragment->GetSetInfo(ShaderDataSetIndex);
        auto shaderData = passData.memoryPool->BeginSet(shaderDataSetInfo);
        {
          auto shaderDataBuffer = passData.memoryPool->GetUniformBufferData<FinalGathererShader::DataBuffer>("FinalGathererData");

          shaderDataBuffer->viewMatrix = passData.viewMatrix;
          shaderDataBuffer->projMatrix = passData.projMatrix;
          shaderDataBuffer->viewportSize = glm::vec4(this->viewportExtent.width, this->viewportExtent.height, 0.0f, 0.0f);
        }
        passData.memoryPool->EndSet();

        std::vector<legit::ImageSamplerBinding> imageSamplerBindings;
        auto directLightImageView = passContext.GetImageView(this->viewportResources->directLight.imageViewProxy->Id());
        imageSamplerBindings.push_back(shaderDataSetInfo->MakeImageSamplerBinding("directLightSampler", directLightImageView, screenspaceSampler.get()));
        auto albedoImageView = passContext.GetImageView(this->viewportResources->albedo.imageViewProxy->Id());
        imageSamplerBindings.push_back(shaderDataSetInfo->MakeImageSamplerBinding("albedoSampler", albedoImageView, screenspaceSampler.get()));
        auto indirectLightView = passContext.GetImageView(this->viewportResources->indirectLight.imageViewProxy->Id());
        imageSamplerBindings.push_back(shaderDataSetInfo->MakeImageSamplerBinding("indirectLightSampler", indirectLightView, screenspaceSampler.get()));

        auto shaderDataSetBindings = legit::DescriptorSetBindings()
          .SetUniformBufferBindings(shaderData.uniformBufferBindings)
          .SetImageSamplerBindings(imageSamplerBindings);
          //.SetStorageImageBindings(storageImageBindings);

        auto shaderDataSet = this->core->GetDescriptorSetCache()->GetDescriptorSet(*shaderDataSetInfo, shaderDataSetBindings);

        passContext.GetCommandBuffer().bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeineInfo.pipelineLayout, ShaderDataSetIndex, { shaderDataSet }, { shaderData.dynamicOffset });
        passContext.GetCommandBuffer().draw(4, 1, 0, 0);
      }
    }));
  }
  void ReloadShaders()
  {
    shadowmapBuilderShader.vertex.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/Common/shadowmapBuilder.vert.spv"));
    shadowmapBuilderShader.fragment.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/Common/shadowmapBuilder.frag.spv"));
    shadowmapBuilderShader.program.reset(new legit::ShaderProgram(shadowmapBuilderShader.vertex.get(), shadowmapBuilderShader.fragment.get()));

    gBufferBuilderShader.vertex.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/Common/gBufferBuilder.vert.spv"));
    gBufferBuilderShader.fragment.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/Common/gBufferBuilder.frag.spv"));
    gBufferBuilderShader.program.reset(new legit::ShaderProgram(gBufferBuilderShader.vertex.get(), gBufferBuilderShader.fragment.get()));

    directLightingShader.vertex.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/Common/screenspaceQuad.vert.spv"));
    directLightingShader.fragment.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/Common/directLighting.frag.spv"));
    directLightingShader.program.reset(new legit::ShaderProgram(directLightingShader.vertex.get(), directLightingShader.fragment.get()));

    indirectLightingShader.compute.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/LSGI/indirectLighting.comp.spv"));
    indirectRayShader.compute.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/LSGI/indirectRay.comp.spv"));
    sweepPrecompShader.compute.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/LSGI/sweepPrecomp.comp.spv"));
    clearShader.compute.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/LSGI/clearBuffer.comp.spv"));

    finalGathererShader.vertex.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/Common/screenspaceQuad.vert.spv"));
    finalGathererShader.fragment.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/LSGI/finalGatherer.frag.spv"));
    finalGathererShader.program.reset(new legit::ShaderProgram(finalGathererShader.vertex.get(), finalGathererShader.fragment.get()));

    denoiserShader.vertex.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/Common/screenspaceQuad.vert.spv"));
    denoiserShader.fragment.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/Common/denoiser.frag.spv"));
    denoiserShader.program.reset(new legit::ShaderProgram(denoiserShader.vertex.get(), denoiserShader.fragment.get()));

    mipBuilder.ReloadShaders();
    blurBuilder.ReloadShaders();
    debugRenderer.ReloadShaders();
  }
private:


  const static uint32_t ShaderDataSetIndex = 0;
  const static uint32_t DrawCallDataSetIndex = 1;

  legit::VertexDeclaration vertexDecl;

  struct ViewportResources
  {
    ViewportResources(legit::RenderGraph *renderGraph, glm::uvec2 screenSize) :
      albedo(renderGraph, vk::Format::eR16G16B16A16Sfloat, screenSize, legit::colorImageUsage),
      emissive(renderGraph, vk::Format::eR16G16B16A16Sfloat, screenSize, legit::colorImageUsage),
      normal(renderGraph, vk::Format::eR16G16B16A16Sfloat, screenSize, legit::colorImageUsage),
      dNormal(renderGraph, vk::Format::eR16G16B16A16Sfloat, screenSize, legit::colorImageUsage),
      depthMoments(renderGraph, vk::Format::eR32G32Sfloat, screenSize, legit::colorImageUsage),
      blurredDepthMoments(renderGraph, vk::Format::eR32G32Sfloat, screenSize, legit::colorImageUsage),
      depthStencil(renderGraph, vk::Format::eD32Sfloat, screenSize, legit::depthImageUsage),
      dDepthStencil(renderGraph, vk::Format::eR32Sfloat, screenSize, legit::colorImageUsage),
      directLight(renderGraph, vk::Format::eR16G16B16A16Sfloat, screenSize, legit::colorImageUsage),
      dDirectLight(renderGraph, vk::Format::eR16G16B16A16Sfloat, screenSize, legit::colorImageUsage),
      dRayDepth(renderGraph, vk::Format::eR32Sfloat, screenSize, legit::colorImageUsage | vk::ImageUsageFlagBits::eStorage),
      blurredDirectLight(renderGraph, vk::Format::eR16G16B16A16Sfloat, screenSize, legit::colorImageUsage),
      shadowMap(renderGraph, vk::Format::eD32Sfloat, glm::uvec2(1024, 1024), legit::depthImageUsage),
      denoisedIndirectLight(renderGraph, vk::Format::eR16G16B16A16Sfloat, screenSize, legit::colorImageUsage),
      indirectLight(renderGraph, vk::Format::eR32G32B32A32Sfloat, screenSize, legit::colorImageUsage | vk::ImageUsageFlagBits::eStorage),
      lineSweepStackImage(renderGraph, vk::Format::eR16Uint, screenSize, vk::ImageUsageFlagBits::eStorage)
    {
      lineSweepStackBuf = renderGraph->AddBuffer<RGB32f>(screenSize.x * screenSize.y);
    }

    #pragma pack(push, 1)
    struct RGB32f
    {
      float r, g, b, a;
    };
    #pragma pack(pop)

    UnmippedProxy albedo;
    UnmippedProxy emissive;
    UnmippedProxy normal;
    UnmippedProxy dNormal;
    MippedProxy depthMoments;
    MippedProxy blurredDepthMoments;
    UnmippedProxy depthStencil;
    UnmippedProxy dDepthStencil;
    UnmippedProxy dRayDepth;
    MippedProxy directLight;
    UnmippedProxy dDirectLight;
    MippedProxy blurredDirectLight;
    UnmippedProxy shadowMap;
    legit::RenderGraph::BufferProxyUnique lineSweepStackBuf;
    UnmippedProxy lineSweepStackImage;
    UnmippedProxy denoisedIndirectLight;
    UnmippedProxy indirectLight;
  };

  struct GBufferBuilderShader
  {
#pragma pack(push, 1)
    struct DataBuffer
    {
      glm::mat4 viewMatrix;
      glm::mat4 projMatrix;
      float time;
      float bla;
    };
#pragma pack(pop)

    std::unique_ptr<legit::Shader> vertex;
    std::unique_ptr<legit::Shader> fragment;
    std::unique_ptr<legit::ShaderProgram> program;
  } gBufferBuilderShader;

  struct ShadowmapBuilderShader
  {
#pragma pack(push, 1)
    struct DataBuffer
    {
      glm::mat4 lightViewMatrix;
      glm::mat4 lightProjMatrix;
    };
#pragma pack(pop)

    std::unique_ptr<legit::Shader> vertex;
    std::unique_ptr<legit::Shader> fragment;
    std::unique_ptr<legit::ShaderProgram> program;
  } shadowmapBuilderShader;

  struct DirectLightingShader
  {
#pragma pack(push, 1)
    struct DataBuffer
    {
      glm::mat4 viewMatrix;
      glm::mat4 projMatrix;
      glm::mat4 lightViewMatrix;
      glm::mat4 lightProjMatrix;
      float time;
    };
#pragma pack(pop)

    std::unique_ptr<legit::Shader> vertex;
    std::unique_ptr<legit::Shader> fragment;
    std::unique_ptr<legit::ShaderProgram> program;
  } directLightingShader;

  struct ClearShader
  {
#pragma pack(push, 1)
    struct DataBuffer
    {
      glm::vec4 viewportSize;
    };
#pragma pack(pop)

    std::unique_ptr<legit::Shader> compute;
  } clearShader;

  struct IndirectLightingShader
  {
#pragma pack(push, 1)
    struct DataBuffer
    {
      glm::mat4 viewMatrix;
      glm::mat4 projMatrix;
      glm::vec4 viewportSize;
      float time;
      int batchesCount;
      int batchIndex;
    };
#pragma pack(pop)

    std::unique_ptr<legit::Shader> compute;
  } indirectLightingShader;

#pragma pack(push, 1)
  struct SweepDataBuffer
  {
    glm::mat4 viewMatrix;
    glm::mat4 projMatrix;
    glm::vec4 viewportSize;
    float time;
    int batchesCount;
    int batchIndex;
    int currLevel;
  };
#pragma pack(pop)

  struct IndirectRayShader
  {
    std::unique_ptr<legit::Shader> compute;
  } indirectRayShader;

  struct SweepPrecompShader
  {
    std::unique_ptr<legit::Shader> compute;
  } sweepPrecompShader;

  struct DenoiserShader
  {
#pragma pack(push, 1)
    struct DataBuffer
    {
      glm::mat4 viewMatrix;
      glm::mat4 projMatrix;
      glm::vec4 viewportSize;
      int radius;
    };
#pragma pack(pop)

    std::unique_ptr<legit::Shader> vertex;
    std::unique_ptr<legit::Shader> fragment;
    std::unique_ptr<legit::ShaderProgram> program;
  } denoiserShader;

  struct FinalGathererShader
  {
#pragma pack(push, 1)
    struct DataBuffer
    {
      glm::mat4 viewMatrix;
      glm::mat4 projMatrix;
      glm::vec4 viewportSize;
    };
#pragma pack(pop)

    std::unique_ptr<legit::Shader> vertex;
    std::unique_ptr<legit::Shader> fragment;
    std::unique_ptr<legit::ShaderProgram> program;
  } finalGathererShader;

  vk::Extent2D viewportExtent;
  std::unique_ptr<ViewportResources> viewportResources;
  MipBuilder mipBuilder;
  BlurBuilder blurBuilder;
  InterleaveBuilder interleaveBuilder;
  DebugRenderer debugRenderer;

  std::unique_ptr<legit::Sampler> screenspaceSampler;
  std::unique_ptr<legit::Sampler> shadowmapSampler;

  legit::Core *core;

};