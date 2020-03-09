#include "../Common/MipBuilder.h"
#include "../Common/BlurBuilder.h"
#include "../Common/DebugRenderer.h"

class SSVCTGIRenderer
{
public:
  SSVCTGIRenderer(legit::Core *_core) :
    mipBuilder(_core),
    blurBuilder(_core),
    debugRenderer(_core)
  {
    this->core = _core;
    descriptorSetCache.reset(new legit::DescriptorSetCache(core->GetLogicalDevice()));
    pipelineCache.reset(new legit::PipelineCache(core->GetLogicalDevice(), descriptorSetCache.get()));

    vertexDecl = GetVertexDeclaration();

    screenspaceSampler.reset(new legit::Sampler(core->GetLogicalDevice(), vk::SamplerAddressMode::eClampToEdge, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear));
    shadowmapSampler.reset(new legit::Sampler(core->GetLogicalDevice(), vk::SamplerAddressMode::eClampToEdge, vk::Filter::eLinear, vk::SamplerMipmapMode::eNearest, true));
    ReloadShaders();
  }
  void ResizeViewport(vk::Extent2D _viewportSize)
  {
    this->viewportSize = viewportSize;
    frameResourcesDatum.clear();
  }
  void RenderFrame(const legit::InFlightQueue::FrameInfo &frameInfo, const Camera &camera, const Camera &light, Scene *scene, GLFWwindow *window)
  {
    #pragma pack(push, 1)
    struct DrawCallDataBuffer
    {
      glm::mat4 modelMatrix;
      glm::vec4 albedoColor;
      glm::vec4 emissiveColor;
    };
    #pragma pack(pop)

    auto &frameResources = frameResourcesDatum[frameInfo.renderGraph];
    if (!frameResources)
    {
      glm::uvec2 size = { frameInfo.viewportSize.width, frameInfo.viewportSize.height };
      frameResources.reset(new FrameResources(frameInfo.renderGraph, size));
    }

    struct PassData
    {
      legit::ShaderMemoryPool *memoryPool;
      //legit::RenderGraph::ImageViewProxyId swapchainImageViewProxyId;
      vk::Extent2D viewportSize;
      glm::mat4 viewMatrix;
      glm::mat4 projMatrix;
      glm::mat4 lightViewMatrix;
      glm::mat4 lightProjMatrix;
      FrameResources *frameResources;
      Scene *scene;
    }passData;

    passData.memoryPool = frameInfo.memoryPool;
    passData.viewportSize = frameInfo.viewportSize;
    passData.scene = scene;
    passData.viewMatrix = glm::inverse(camera.GetTransformMatrix());
    passData.lightViewMatrix = glm::inverse(light.GetTransformMatrix());
    passData.frameResources = frameResources.get();
    //passData.swapchainImageViewProxyId = frameInfo.swapchainImageViewProxyId;
    float aspect = float(frameInfo.viewportSize.width) / float(frameInfo.viewportSize.height);
    passData.projMatrix = glm::perspective(1.0f, aspect, 0.01f, 1000.0f) * glm::scale(glm::vec3(1.0f, -1.0f, -1.0f));
    passData.lightProjMatrix = glm::perspective(0.8f, 1.0f, 0.1f, 100.0f) * glm::scale(glm::vec3(1.0f, -1.0f, -1.0f));

    //rendering shadow map
    vk::Extent2D shadowMapExtent(frameResources->shadowMap.baseSize.x, frameResources->shadowMap.baseSize.y);
    frameInfo.renderGraph->AddRenderPass(
      {
      }, frameResources->shadowMap.imageViewProxy->Id(),
      {}, shadowMapExtent, vk::AttachmentLoadOp::eClear, [this, passData](legit::RenderGraph::RenderPassContext passContext)
    {
      auto pipeineInfo = this->core->GetPipelineCache()->BindGraphicsPipeline(passContext.GetCommandBuffer(), passContext.GetRenderPass()->GetHandle(), legit::DepthSettings::DepthTest(), { legit::BlendSettings::Opaque() }, vertexDecl, vk::PrimitiveTopology::eTriangleList, shadowmapBuilderShader.program.get());
      {
        const legit::DescriptorSetLayoutKey *shaderDataSetInfo = shadowmapBuilderShader.vertex->GetSetInfo(ShaderDataSetIndex);
        auto shaderDataOffset = passData.memoryPool->BeginSet(shaderDataSetInfo);
        {
          auto shaderDataBuffer = passData.memoryPool->GetBufferData<ShadowmapBuilderShader::DataBuffer>("ShadowmapBuilderData");

          shaderDataBuffer->lightViewMatrix = passData.lightViewMatrix;
          shaderDataBuffer->lightProjMatrix = passData.lightProjMatrix;
        }
        passData.memoryPool->EndSet();
        auto shaderDataSet = this->core->GetDescriptorSetCache()->GetDescriptorSet(*shaderDataSetInfo, passData.memoryPool->GetBuffer()->GetHandle(), {});

        const legit::DescriptorSetLayoutKey *drawCallSetInfo = shadowmapBuilderShader.vertex->GetSetInfo(DrawCallDataSetIndex);

        passData.scene->IterateObjects([&](glm::mat4 objectToWorld, glm::vec3 albedoColor, glm::vec3 emissiveColor, vk::Buffer vertexBuffer, vk::Buffer indexBuffer, uint32_t indicesCount)
        {
          uint32_t drawCallDataOffset = passData.memoryPool->BeginSet(drawCallSetInfo);
          {
            auto drawCallData = passData.memoryPool->GetBufferData<DrawCallDataBuffer>("DrawCallData");
            drawCallData->modelMatrix = objectToWorld;
            drawCallData->albedoColor = glm::vec4(albedoColor, 1.0f);
            drawCallData->emissiveColor = glm::vec4(emissiveColor, 1.0f);
          }
          passData.memoryPool->EndSet();
          auto drawCallSet = this->core->GetDescriptorSetCache()->GetDescriptorSet(*drawCallSetInfo, passData.memoryPool->GetBuffer()->GetHandle(), {});
          passContext.GetCommandBuffer().bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeineInfo.pipelineLayout, ShaderDataSetIndex,
            { shaderDataSet, drawCallSet },
            { shaderDataOffset, drawCallDataOffset });

          passContext.GetCommandBuffer().bindVertexBuffers(0, { vertexBuffer }, { 0 });
          passContext.GetCommandBuffer().bindIndexBuffer(indexBuffer, 0, vk::IndexType::eUint32);
          passContext.GetCommandBuffer().drawIndexed(indicesCount, 1, 0, 0, 0);
        });
      }
    });

    //rendering gbuffer
    frameInfo.renderGraph->AddRenderPass(
      { 
        frameResources->albedo.imageViewProxy->Id(),       //location = 0
        frameResources->emissive.imageViewProxy->Id(),     //location = 1
        frameResources->normal.imageViewProxy->Id(),       //location = 2
        frameResources->depthMoments.mipImageViewProxies[0]->Id(), //location = 3
      }, frameResources->depthStencil.imageViewProxy->Id(),
      {}, frameInfo.viewportSize, vk::AttachmentLoadOp::eClear, [this, passData](legit::RenderGraph::RenderPassContext passContext)
    {
      std::vector<legit::BlendSettings> attachmentBlendSettings;
      attachmentBlendSettings.resize(passContext.GetRenderPass()->GetColorAttachmentsCount(), legit::BlendSettings::Opaque());
      auto pipeineInfo = this->core->GetPipelineCache()->BindGraphicsPipeline(passContext.GetCommandBuffer(), passContext.GetRenderPass()->GetHandle(), legit::DepthSettings::DepthTest(), attachmentBlendSettings, vertexDecl, vk::PrimitiveTopology::eTriangleList, gBufferBuilderShader.program.get());
      {
        const legit::DescriptorSetLayoutKey *shaderDataSetInfo = gBufferBuilderShader.vertex->GetSetInfo(ShaderDataSetIndex);
        uint32_t shaderDataOffset = passData.memoryPool->BeginSet(shaderDataSetInfo);
        {
          auto shaderDataBuffer = passData.memoryPool->GetBufferData<GBufferBuilderShader::DataBuffer>("GBufferBuilderData");

          shaderDataBuffer->time = 0.0f;
          shaderDataBuffer->projMatrix = passData.projMatrix;
          shaderDataBuffer->viewMatrix = passData.viewMatrix;
        }
        passData.memoryPool->EndSet();
        auto shaderDataSet = this->core->GetDescriptorSetCache()->GetDescriptorSet(*shaderDataSetInfo, passData.memoryPool->GetBuffer()->GetHandle(), {});

        const legit::DescriptorSetLayoutKey *drawCallSetInfo = gBufferBuilderShader.vertex->GetSetInfo(DrawCallDataSetIndex);

        passData.scene->IterateObjects([&](glm::mat4 objectToWorld, glm::vec3 albedoColor, glm::vec3 emissiveColor, vk::Buffer vertexBuffer , vk::Buffer indexBuffer, uint32_t indicesCount)
        {
          uint32_t drawCallDataOffset = passData.memoryPool->BeginSet(drawCallSetInfo);
          {
            auto drawCallData = passData.memoryPool->GetBufferData<DrawCallDataBuffer>("DrawCallData");
            drawCallData->modelMatrix = objectToWorld;
            drawCallData->albedoColor = glm::vec4(albedoColor, 1.0f);
            drawCallData->emissiveColor = glm::vec4(emissiveColor, 1.0f);
          }
          passData.memoryPool->EndSet();
          auto drawCallSet = this->core->GetDescriptorSetCache()->GetDescriptorSet(*drawCallSetInfo, passData.memoryPool->GetBuffer()->GetHandle(), {});
          passContext.GetCommandBuffer().bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeineInfo.pipelineLayout, ShaderDataSetIndex,
            { shaderDataSet, drawCallSet },
            { shaderDataOffset, drawCallDataOffset });

          passContext.GetCommandBuffer().bindVertexBuffers(0, { vertexBuffer }, { 0 });
          passContext.GetCommandBuffer().bindIndexBuffer(indexBuffer, 0, vk::IndexType::eUint32);
          passContext.GetCommandBuffer().drawIndexed(indicesCount, 1, 0, 0, 0);
        });
      }
    });

    //applying direct lighting
    frameInfo.renderGraph->AddRenderPass(
      {
        frameResources->directLight.mipImageViewProxies[0]->Id(),       //location = 0
      }, legit::RenderGraph::ImageViewProxyId(),
      {
        frameResources->albedo.imageViewProxy->Id(),
        frameResources->emissive.imageViewProxy->Id(),
        frameResources->normal.imageViewProxy->Id(),
        frameResources->depthStencil.imageViewProxy->Id(),
        frameResources->shadowMap.imageViewProxy->Id(),
      },
      frameInfo.viewportSize, vk::AttachmentLoadOp::eDontCare, [this, passData](legit::RenderGraph::RenderPassContext passContext)
    {
      auto pipeineInfo = this->core->GetPipelineCache()->BindGraphicsPipeline(passContext.GetCommandBuffer(), passContext.GetRenderPass()->GetHandle(), legit::DepthSettings::Disabled(), { legit::BlendSettings::Opaque() }, legit::VertexDeclaration(), vk::PrimitiveTopology::eTriangleFan, directLightingShader.program.get());
      {
        const legit::DescriptorSetLayoutKey *shaderDataSetInfo = directLightingShader.fragment->GetSetInfo(ShaderDataSetIndex);
        uint32_t shaderDataOffset = passData.memoryPool->BeginSet(shaderDataSetInfo);
        {
          auto shaderDataBuffer = passData.memoryPool->GetBufferData<DirectLightingShader::DataBuffer>("DirectLightingData");

          shaderDataBuffer->viewMatrix = passData.viewMatrix;
          shaderDataBuffer->projMatrix = passData.projMatrix;
          shaderDataBuffer->lightViewMatrix = passData.lightViewMatrix;
          shaderDataBuffer->lightProjMatrix = passData.lightProjMatrix;
          shaderDataBuffer->time = 0.0f;
        }
        passData.memoryPool->EndSet();
        std::vector<legit::ImageSamplerBinding> imageSamplerBindings;
        auto albedoImageView = passContext.GetImageView(passData.frameResources->albedo.imageViewProxy->Id());
        imageSamplers.push_back(legit::DescriptorSetCache::ImageSampler(albedoImageView, screenspaceSampler.get(), "albedoSampler"));
        auto emissiveImageView = passContext.GetImageView(passData.frameResources->emissive.imageViewProxy->Id());
        imageSamplers.push_back(legit::DescriptorSetCache::ImageSampler(emissiveImageView, screenspaceSampler.get(), "emissiveSampler"));
        auto normalImageView = passContext.GetImageView(passData.frameResources->normal.imageViewProxy->Id());
        imageSamplers.push_back(legit::DescriptorSetCache::ImageSampler(normalImageView, screenspaceSampler.get(), "normalSampler"));
        auto depthStencilImageView = passContext.GetImageView(passData.frameResources->depthStencil.imageViewProxy->Id());
        imageSamplers.push_back(legit::DescriptorSetCache::ImageSampler(depthStencilImageView, screenspaceSampler.get(), "depthStencilSampler"));
        auto shadowmapImageView = passContext.GetImageView(passData.frameResources->shadowMap.imageViewProxy->Id());
        imageSamplers.push_back(legit::DescriptorSetCache::ImageSampler(shadowmapImageView, shadowmapSampler.get(), "shadowmapSampler"));

        auto shaderDataSet = this->core->GetDescriptorSetCache()->GetDescriptorSet(*shaderDataSetInfo, passData.memoryPool->GetBuffer()->GetHandle(), imageSamplers);

        passContext.GetCommandBuffer().bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeineInfo.pipelineLayout, ShaderDataSetIndex, { shaderDataSet }, { shaderDataOffset });
        passContext.GetCommandBuffer().draw(4, 1, 0, 0);
      }
    });

    mipBuilder.BuildMips(frameInfo.renderGraph, frameInfo.memoryPool, frameResources->directLight);
    mipBuilder.BuildMips(frameInfo.renderGraph, frameInfo.memoryPool, frameResources->depthMoments);
    for (uint32_t mipLevel = 0; mipLevel < frameResources->blurredDirectLight.mipImageViewProxies.size(); mipLevel++)
    {
      auto srcImageViewProxyId = frameResources->directLight.mipImageViewProxies[mipLevel];
      auto dstImageViewProxyId = frameResources->blurredDirectLight.mipImageViewProxies[mipLevel];
      blurBuilder.ApplyBlur(frameInfo.renderGraph, frameInfo.memoryPool, srcImageViewProxyId, dstImageViewProxyId, mipLevel == 0 ? 0 : 2);
    }

    for (uint32_t mipLevel = 0; mipLevel < frameResources->blurredDirectLight.mipImageViewProxies.size(); mipLevel++)
    {
      auto srcImageViewProxyId = frameResources->depthMoments.mipImageViewProxies[mipLevel];
      auto dstImageViewProxyId = frameResources->blurredDepthMoments.mipImageViewProxies[mipLevel];
      blurBuilder.ApplyBlur(frameInfo.renderGraph, frameInfo.memoryPool, srcImageViewProxyId, dstImageViewProxyId, mipLevel == 0 ? 0 : 2);
    }

    //calculating indirect lighting
    frameInfo.renderGraph->AddRenderPass(
      {
        frameResources->indirectLight.imageViewProxy->Id()
      }, legit::RenderGraph::ImageViewProxyId(),
      {
        frameResources->blurredDirectLight.imageViewProxy->Id(),
        frameResources->blurredDepthMoments.imageViewProxy->Id(),
        frameResources->normal.imageViewProxy->Id(),
        frameResources->depthStencil.imageViewProxy->Id(),
      },
      frameInfo.viewportSize, vk::AttachmentLoadOp::eDontCare, [this, passData](legit::RenderGraph::RenderPassContext passContext)
    {
      auto pipeineInfo = this->core->GetPipelineCache()->BindGraphicsPipeline(passContext.GetCommandBuffer(), passContext.GetRenderPass()->GetHandle(), legit::DepthSettings::Disabled(), { legit::BlendSettings::Opaque() }, legit::VertexDeclaration(), vk::PrimitiveTopology::eTriangleFan, indirectLightingShader.program.get());
      {
        const legit::DescriptorSetLayoutKey *shaderDataSetInfo = indirectLightingShader.fragment->GetSetInfo(ShaderDataSetIndex);
        uint32_t shaderDataOffset = passData.memoryPool->BeginSet(shaderDataSetInfo);
        {
          auto shaderDataBuffer = passData.memoryPool->GetBufferData<IndirectLightingShader::DataBuffer>("IndirectLightingData");

          shaderDataBuffer->viewMatrix = passData.viewMatrix;
          shaderDataBuffer->projMatrix = passData.projMatrix;
          shaderDataBuffer->viewportSize = glm::vec4(passData.viewportSize.width, passData.viewportSize.height, 0.0f, 0.0f);
        }
        passData.memoryPool->EndSet();

        std::vector<legit::ImageSamplerBinding> imageSamplerBindings;
        auto blurredDirectLightImageView = passContext.GetImageView(passData.frameResources->blurredDirectLight.imageViewProxy->Id());
        imageSamplers.push_back(legit::DescriptorSetCache::ImageSampler(blurredDirectLightImageView, screenspaceSampler.get(), "blurredDirectLightSampler"));
        auto blurredDepthMomentsImageView = passContext.GetImageView(passData.frameResources->blurredDepthMoments.imageViewProxy->Id());
        imageSamplers.push_back(legit::DescriptorSetCache::ImageSampler(blurredDepthMomentsImageView, screenspaceSampler.get(), "blurredDepthMomentsSampler"));
        auto normalImageView = passContext.GetImageView(passData.frameResources->normal.imageViewProxy->Id());
        imageSamplers.push_back(legit::DescriptorSetCache::ImageSampler(normalImageView, screenspaceSampler.get(), "normalSampler"));
        auto depthStencilImageView = passContext.GetImageView(passData.frameResources->depthStencil.imageViewProxy->Id());
        imageSamplers.push_back(legit::DescriptorSetCache::ImageSampler(depthStencilImageView, screenspaceSampler.get(), "depthStencilSampler"));

        auto shaderDataSet = this->core->GetDescriptorSetCache()->GetDescriptorSet(*shaderDataSetInfo, passData.memoryPool->GetBuffer()->GetHandle(), imageSamplers);

        passContext.GetCommandBuffer().bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeineInfo.pipelineLayout, ShaderDataSetIndex, { shaderDataSet }, { shaderDataOffset });
        passContext.GetCommandBuffer().draw(4, 1, 0, 0);
      }
    });

    //denoising indirect lighting
    frameInfo.renderGraph->AddRenderPass(
      {
        frameResources->denoisedIndirectLight.imageViewProxy->Id()
      }, legit::RenderGraph::ImageViewProxyId(),
      {
        frameResources->depthMoments.imageViewProxy->Id(),
        frameResources->normal.imageViewProxy->Id(),
        frameResources->indirectLight.imageViewProxy->Id()
      },
      frameInfo.viewportSize, vk::AttachmentLoadOp::eDontCare, [this, passData, window](legit::RenderGraph::RenderPassContext passContext)
    {
      auto pipeineInfo = this->core->GetPipelineCache()->BindGraphicsPipeline(
        passContext.GetCommandBuffer(), passContext.GetRenderPass()->GetHandle(), 
        legit::DepthSettings::Disabled(), { legit::BlendSettings::Opaque() }, legit::VertexDeclaration(), vk::PrimitiveTopology::eTriangleFan, denoiserShader.program.get());
      {
        const legit::DescriptorSetLayoutKey *shaderDataSetInfo = denoiserShader.fragment->GetSetInfo(ShaderDataSetIndex);
        uint32_t shaderDataOffset = passData.memoryPool->BeginSet(shaderDataSetInfo);
        {
          auto shaderDataBuffer = passData.memoryPool->GetBufferData<DenoiserShader::DataBuffer>("DenoiserData");

          shaderDataBuffer->viewMatrix = passData.viewMatrix;
          shaderDataBuffer->projMatrix = passData.projMatrix;
          shaderDataBuffer->viewportSize = glm::vec4(passData.viewportSize.width, passData.viewportSize.height, 0.0f, 0.0f);
          shaderDataBuffer->radius = (!glfwGetKey(window, GLFW_KEY_A) ? 0 : 2);
        }
        passData.memoryPool->EndSet();

        std::vector<legit::ImageSamplerBinding> imageSamplerBindings;
        imageSamplers.push_back(legit::DescriptorSetCache::ImageSampler(passContext.GetImageView(passData.frameResources->depthMoments.imageViewProxy->Id()), screenspaceSampler.get(), "depthStencilSampler"));
        imageSamplers.push_back(legit::DescriptorSetCache::ImageSampler(passContext.GetImageView(passData.frameResources->normal.imageViewProxy->Id()), screenspaceSampler.get(), "normalSampler"));
        imageSamplers.push_back(legit::DescriptorSetCache::ImageSampler(passContext.GetImageView(passData.frameResources->indirectLight.imageViewProxy->Id()), screenspaceSampler.get(), "noisySampler"));

        auto shaderDataSet = this->core->GetDescriptorSetCache()->GetDescriptorSet(*shaderDataSetInfo, passData.memoryPool->GetBuffer()->GetHandle(), imageSamplers);

        passContext.GetCommandBuffer().bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeineInfo.pipelineLayout, ShaderDataSetIndex, { shaderDataSet }, { shaderDataOffset });
        passContext.GetCommandBuffer().draw(4, 1, 0, 0);
      }
    });

    //final gathering
    frameInfo.renderGraph->AddRenderPass(
      {
        frameInfo.swapchainImageViewProxyId,       //location = 0
      }, legit::RenderGraph::ImageViewProxyId(),
      {
        frameResources->directLight.imageViewProxy->Id(),
        frameResources->blurredDirectLight.imageViewProxy->Id(),
        frameResources->albedo.imageViewProxy->Id(),
        frameResources->denoisedIndirectLight.imageViewProxy->Id()
      },
      frameInfo.viewportSize, vk::AttachmentLoadOp::eClear, [this, passData](legit::RenderGraph::RenderPassContext passContext)
    {
      auto pipeineInfo = this->core->GetPipelineCache()->BindGraphicsPipeline(passContext.GetCommandBuffer(), passContext.GetRenderPass()->GetHandle(), legit::DepthSettings::Disabled(), { legit::BlendSettings::Opaque() }, legit::VertexDeclaration(), vk::PrimitiveTopology::eTriangleFan, finalGathererShader.program.get());
      {
        const legit::DescriptorSetLayoutKey *shaderDataSetInfo = finalGathererShader.fragment->GetSetInfo(ShaderDataSetIndex);
        uint32_t shaderDataOffset = passData.memoryPool->BeginSet(shaderDataSetInfo);
        {
          auto shaderDataBuffer = passData.memoryPool->GetBufferData<FinalGathererShader::DataBuffer>("FinalGathererData");

          shaderDataBuffer->viewMatrix = passData.viewMatrix;
          shaderDataBuffer->projMatrix = passData.projMatrix;
        }
        passData.memoryPool->EndSet();

        std::vector<legit::ImageSamplerBinding> imageSamplerBindings;
        auto directLightImageView = passContext.GetImageView(passData.frameResources->directLight.imageViewProxy->Id());
        imageSamplers.push_back(legit::DescriptorSetCache::ImageSampler(directLightImageView, screenspaceSampler.get(), "directLightSampler"));
        auto blurredDirectLightImageView = passContext.GetImageView(passData.frameResources->blurredDirectLight.imageViewProxy->Id());
        imageSamplers.push_back(legit::DescriptorSetCache::ImageSampler(blurredDirectLightImageView, screenspaceSampler.get(), "blurredDirectLightSampler"));
        auto albedoImageView = passContext.GetImageView(passData.frameResources->albedo.imageViewProxy->Id());
        imageSamplers.push_back(legit::DescriptorSetCache::ImageSampler(albedoImageView, screenspaceSampler.get(), "albedoSampler"));
        auto indirectLightImageView = passContext.GetImageView(passData.frameResources->denoisedIndirectLight.imageViewProxy->Id());
        imageSamplers.push_back(legit::DescriptorSetCache::ImageSampler(indirectLightImageView, screenspaceSampler.get(), "indirectLightSampler"));

        auto shaderDataSet = this->core->GetDescriptorSetCache()->GetDescriptorSet(*shaderDataSetInfo, passData.memoryPool->GetBuffer()->GetHandle(), imageSamplers);

        passContext.GetCommandBuffer().bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeineInfo.pipelineLayout, ShaderDataSetIndex, { shaderDataSet }, { shaderDataOffset });
        passContext.GetCommandBuffer().draw(4, 1, 0, 0);
      }
    });

    std::vector<legit::RenderGraph::ImageViewProxyId> debugProxies;
    //debugProxies = frameResources->blurredDirectLight.mipImageViewProxies;
    debugProxies.push_back(frameResources->indirectLight.imageViewProxy->Id());
    debugProxies.push_back(frameResources->denoisedIndirectLight.imageViewProxy->Id());
    debugRenderer.RenderImageViews(frameInfo.renderGraph, frameInfo.memoryPool, frameInfo.swapchainImageViewProxyId, debugProxies);
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

    indirectLightingShader.vertex.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/Common/screenspaceQuad.vert.spv"));
    indirectLightingShader.fragment.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/SSVCTGI/indirectLighting.frag.spv"));
    indirectLightingShader.program.reset(new legit::ShaderProgram(indirectLightingShader.vertex.get(), indirectLightingShader.fragment.get()));

    finalGathererShader.vertex.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/Common/screenspaceQuad.vert.spv"));
    finalGathererShader.fragment.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/Common/finalGatherer.frag.spv"));
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

  struct FrameResources
  {
    FrameResources(legit::RenderGraph *renderGraph, glm::uvec2 screenSize) :
      albedo(renderGraph, vk::Format::eR16G16B16A16Sfloat, screenSize),
      emissive(renderGraph, vk::Format::eR16G16B16A16Sfloat, screenSize),
      normal(renderGraph, vk::Format::eR16G16B16A16Sfloat, screenSize),
      depthMoments(renderGraph, vk::Format::eR32G32Sfloat, screenSize),
      blurredDepthMoments(renderGraph, vk::Format::eR32G32Sfloat, screenSize),
      depthStencil(renderGraph, vk::Format::eD32Sfloat, screenSize),
      directLight(renderGraph, vk::Format::eR16G16B16A16Sfloat, screenSize),
      blurredDirectLight(renderGraph, vk::Format::eR16G16B16A16Sfloat, screenSize),
      shadowMap(renderGraph, vk::Format::eD32Sfloat, glm::uvec2(1024, 1024)),
      indirectLight(renderGraph, vk::Format::eR16G16B16A16Sfloat, screenSize),
      denoisedIndirectLight(renderGraph, vk::Format::eR16G16B16A16Sfloat, screenSize)
    {
    }

    UnmippedProxy albedo;
    UnmippedProxy emissive;
    UnmippedProxy normal;
    MippedProxy depthMoments;
    MippedProxy blurredDepthMoments;
    UnmippedProxy depthStencil;
    MippedProxy directLight;
    MippedProxy blurredDirectLight;
    UnmippedProxy shadowMap;
    UnmippedProxy indirectLight;
    UnmippedProxy denoisedIndirectLight;
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


  struct IndirectLightingShader
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
  } indirectLightingShader;

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
    };
    #pragma pack(pop)

    std::unique_ptr<legit::Shader> vertex;
    std::unique_ptr<legit::Shader> fragment;
    std::unique_ptr<legit::ShaderProgram> program;
  } finalGathererShader;

  vk::Extent2D viewportSize;
  std::map<legit::RenderGraph*, std::unique_ptr<FrameResources> > frameResourcesDatum;
  MipBuilder mipBuilder;
  BlurBuilder blurBuilder;
  DebugRenderer debugRenderer;

  std::unique_ptr<legit::Sampler> screenspaceSampler;
  std::unique_ptr<legit::Sampler> shadowmapSampler;

  legit::Core *core;

};