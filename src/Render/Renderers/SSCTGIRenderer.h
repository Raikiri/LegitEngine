#include "../Common/MipBuilder.h"
#include "../Common/BlurBuilder.h"
#include "../Common/DebugRenderer.h"

class SSCTGIRenderer
{
public:
  SSCTGIRenderer(legit::Core *_core) :
    mipBuilder(_core),
    blurBuilder(_core),
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

    struct PassData
    {
      legit::ShaderMemoryPool *memoryPool;
      //legit::RenderGraph::ImageViewProxyId swapchainImageViewProxyId;
      glm::mat4 viewMatrix;
      glm::mat4 projMatrix;
      glm::mat4 lightViewMatrix;
      glm::mat4 lightProjMatrix;
      Scene *scene;
    }passData;

    passData.memoryPool = frameInfo.memoryPool;
    passData.scene = scene;
    passData.viewMatrix = glm::inverse(camera.GetTransformMatrix());
    passData.lightViewMatrix = glm::inverse(light.GetTransformMatrix());
    //passData.swapchainImageViewProxyId = frameInfo.swapchainImageViewProxyId;
    float aspect = float(viewportExtent.width) / float(viewportExtent.height);
    passData.projMatrix = glm::perspective(1.0f, aspect, 0.01f, 1000.0f) * glm::scale(glm::vec3(1.0f, -1.0f, -1.0f));
    passData.lightProjMatrix = glm::perspective(0.8f, 1.0f, 0.1f, 100.0f) * glm::scale(glm::vec3(1.0f, -1.0f, -1.0f));

    //rendering shadow map
    vk::Extent2D shadowMapExtent(viewportResources->shadowMap.baseSize.x, viewportResources->shadowMap.baseSize.y);
    core->GetRenderGraph()->AddPass(legit::RenderGraph::RenderPassDesc()
      .SetDepthAttachment(viewportResources->shadowMap.imageViewProxy->Id(), vk::AttachmentLoadOp::eClear)
      .SetRenderAreaExtent(shadowMapExtent)
      .SetProfilerInfo(legit::Colors::amethyst, "ShadowPass")
      .SetRecordFunc([this, passData](legit::RenderGraph::RenderPassContext passContext)
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
    }));

    //rendering gbuffer
    core->GetRenderGraph()->AddPass(legit::RenderGraph::RenderPassDesc()
      .SetColorAttachments(
      {
        viewportResources->albedo.imageViewProxy->Id(),       //location = 0
        viewportResources->emissive.imageViewProxy->Id(),     //location = 1
        viewportResources->normal.imageViewProxy->Id(),       //location = 2
        viewportResources->depthMoments.mipImageViewProxies[0]->Id(), //location = 3
      }, vk::AttachmentLoadOp::eClear)
      .SetDepthAttachment(viewportResources->depthStencil.imageViewProxy->Id(), vk::AttachmentLoadOp::eClear)
      .SetRenderAreaExtent(viewportExtent)
      .SetProfilerInfo(legit::Colors::belizeHole, "GBufferPass")
      .SetRecordFunc([this, passData](legit::RenderGraph::RenderPassContext passContext)
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

        passData.scene->IterateObjects([&](glm::mat4 objectToWorld, glm::vec3 albedoColor, glm::vec3 emissiveColor, vk::Buffer vertexBuffer , vk::Buffer indexBuffer, uint32_t verticeCount, uint32_t indicesCount)
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
    }));

    //applying direct lighting
    core->GetRenderGraph()->AddPass(legit::RenderGraph::RenderPassDesc()
      .SetColorAttachments({ viewportResources->directLight.mipImageViewProxies[0]->Id()})
      .SetInputImages({
        viewportResources->albedo.imageViewProxy->Id(),
        viewportResources->emissive.imageViewProxy->Id(),
        viewportResources->normal.imageViewProxy->Id(),
        viewportResources->depthStencil.imageViewProxy->Id(),
        viewportResources->shadowMap.imageViewProxy->Id(),
      })
      .SetRenderAreaExtent(viewportExtent)
      .SetProfilerInfo(legit::Colors::orange, "LightPass")
      .SetRecordFunc([this, passData](legit::RenderGraph::RenderPassContext passContext)
    {
      auto pipeineInfo = this->core->GetPipelineCache()->BindGraphicsPipeline(passContext.GetCommandBuffer(), passContext.GetRenderPass()->GetHandle(), legit::DepthSettings::Disabled(), { legit::BlendSettings::Opaque() }, legit::VertexDeclaration(), vk::PrimitiveTopology::eTriangleFan, directLightingShader.program.get());
      {
        const legit::DescriptorSetLayoutKey *shaderDataSetInfo = directLightingShader.fragment->GetSetInfo(ShaderDataSetIndex);
        auto shaderData = passData.memoryPool->BeginSet(shaderDataSetInfo);
        {
          auto shaderDataBuffer = passData.memoryPool->GetUniformBufferData<DirectLightingShader::DataBuffer>("DirectLightingData");

          shaderDataBuffer->viewMatrix = passData.viewMatrix;
          shaderDataBuffer->projMatrix = passData.projMatrix;
          shaderDataBuffer->lightViewMatrix = passData.lightViewMatrix;
          shaderDataBuffer->lightProjMatrix = passData.lightProjMatrix;
          shaderDataBuffer->time = 0.0f;
        }
        passData.memoryPool->EndSet();
        std::vector<legit::ImageSamplerBinding> imageSamplerBindings;
        auto albedoImageView = passContext.GetImageView(this->viewportResources->albedo.imageViewProxy->Id());
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
    }));

    mipBuilder.BuildMips(core->GetRenderGraph(), frameInfo.memoryPool, viewportResources->directLight);
    //mipBuilder.BuildMips(core->GetRenderGraph(), frameInfo.memoryPool, viewportResources->depthMoments, MipBuilder::FilterTypes::Depth);
    mipBuilder.BuildMips(core->GetRenderGraph(), frameInfo.memoryPool, viewportResources->depthMoments);
    for (uint32_t mipLevel = 0; mipLevel < this->viewportResources->blurredDirectLight.mipImageViewProxies.size(); mipLevel++)
    {
      auto srcImageViewProxyId = viewportResources->directLight.mipImageViewProxies[mipLevel]->Id();
      auto dstImageViewProxyId = viewportResources->blurredDirectLight.mipImageViewProxies[mipLevel]->Id();
      blurBuilder.ApplyBlur(core->GetRenderGraph(), frameInfo.memoryPool, srcImageViewProxyId, dstImageViewProxyId, mipLevel == 0 ? 0 : 2 * 0);
    }

    for (uint32_t mipLevel = 0; mipLevel < viewportResources->blurredDirectLight.mipImageViewProxies.size(); mipLevel++)
    {
      auto srcImageViewProxyId = viewportResources->depthMoments.mipImageViewProxies[mipLevel]->Id();
      auto dstImageViewProxyId = viewportResources->blurredDepthMoments.mipImageViewProxies[mipLevel]->Id();
      blurBuilder.ApplyBlur(core->GetRenderGraph(), frameInfo.memoryPool, srcImageViewProxyId, dstImageViewProxyId, mipLevel == 0 ? 0 : 2 * 0);
    }

    //calculating indirect lighting
    core->GetRenderGraph()->AddPass(legit::RenderGraph::RenderPassDesc()
      .SetColorAttachments({ viewportResources->indirectLight.imageViewProxy->Id()})
      .SetInputImages({
        viewportResources->blurredDirectLight.imageViewProxy->Id(),
        viewportResources->blurredDepthMoments.imageViewProxy->Id(),
        viewportResources->normal.imageViewProxy->Id(),
        viewportResources->depthStencil.imageViewProxy->Id()})
      .SetRenderAreaExtent(viewportExtent)
      .SetProfilerInfo(legit::Colors::sunFlower, "IndirectLightPass")
      .SetRecordFunc([this, passData](legit::RenderGraph::RenderPassContext passContext)
    {
      auto pipeineInfo = this->core->GetPipelineCache()->BindGraphicsPipeline(passContext.GetCommandBuffer(), passContext.GetRenderPass()->GetHandle(), legit::DepthSettings::Disabled(), { legit::BlendSettings::Opaque() }, legit::VertexDeclaration(), vk::PrimitiveTopology::eTriangleFan, indirectLightingShader.program.get());
      {
        const legit::DescriptorSetLayoutKey *shaderDataSetInfo = indirectLightingShader.fragment->GetSetInfo(ShaderDataSetIndex);
        auto shaderData = passData.memoryPool->BeginSet(shaderDataSetInfo);
        {
          auto shaderDataBuffer = passData.memoryPool->GetUniformBufferData<IndirectLightingShader::DataBuffer>("IndirectLightingData");

          shaderDataBuffer->viewMatrix = passData.viewMatrix;
          shaderDataBuffer->projMatrix = passData.projMatrix;
          shaderDataBuffer->viewportSize = glm::vec4(this->viewportExtent.width, this->viewportExtent.height, 0.0f, 0.0f);
        }
        passData.memoryPool->EndSet();

        std::vector<legit::ImageSamplerBinding> imageSamplerBindings;
        auto blurredDirectLightImageView = passContext.GetImageView(this->viewportResources->blurredDirectLight.imageViewProxy->Id());
        imageSamplerBindings.push_back(shaderDataSetInfo->MakeImageSamplerBinding("blurredDirectLightSampler", blurredDirectLightImageView, screenspaceSampler.get()));
        auto blurredDepthMomentsImageView = passContext.GetImageView(this->viewportResources->blurredDepthMoments.imageViewProxy->Id());
        imageSamplerBindings.push_back(shaderDataSetInfo->MakeImageSamplerBinding("blurredDepthMomentsSampler", blurredDepthMomentsImageView, screenspaceSampler.get()));
        auto normalImageView = passContext.GetImageView(this->viewportResources->normal.imageViewProxy->Id());
        imageSamplerBindings.push_back(shaderDataSetInfo->MakeImageSamplerBinding("normalSampler", normalImageView, screenspaceSampler.get()));
        auto depthStencilImageView = passContext.GetImageView(this->viewportResources->depthStencil.imageViewProxy->Id());
        imageSamplerBindings.push_back(shaderDataSetInfo->MakeImageSamplerBinding("depthStencilSampler", depthStencilImageView, screenspaceSampler.get()));

        auto shaderDataSet = this->core->GetDescriptorSetCache()->GetDescriptorSet(*shaderDataSetInfo, shaderData.uniformBufferBindings, {}, imageSamplerBindings);

        passContext.GetCommandBuffer().bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeineInfo.pipelineLayout, ShaderDataSetIndex, { shaderDataSet }, { shaderData.dynamicOffset });
        passContext.GetCommandBuffer().draw(4, 1, 0, 0);
      }
    }));

    //denoising indirect lighting
    core->GetRenderGraph()->AddPass(legit::RenderGraph::RenderPassDesc()
      .SetColorAttachments({viewportResources->denoisedIndirectLight.imageViewProxy->Id()})
      .SetInputImages({
        viewportResources->depthMoments.imageViewProxy->Id(),
        viewportResources->normal.imageViewProxy->Id(),
        viewportResources->indirectLight.imageViewProxy->Id()})
      .SetRenderAreaExtent(viewportExtent)
      .SetProfilerInfo(legit::Colors::greenSea, "DenoiserPass")
      .SetRecordFunc([this, passData, window](legit::RenderGraph::RenderPassContext passContext)
    {
      auto pipeineInfo = this->core->GetPipelineCache()->BindGraphicsPipeline(
        passContext.GetCommandBuffer(), passContext.GetRenderPass()->GetHandle(), 
        legit::DepthSettings::Disabled(), { legit::BlendSettings::Opaque() }, legit::VertexDeclaration(), vk::PrimitiveTopology::eTriangleFan, denoiserShader.program.get());
      {
        const legit::DescriptorSetLayoutKey *shaderDataSetInfo = denoiserShader.fragment->GetSetInfo(ShaderDataSetIndex);
        auto shaderData = passData.memoryPool->BeginSet(shaderDataSetInfo);
        {
          auto shaderDataBuffer = passData.memoryPool->GetUniformBufferData<DenoiserShader::DataBuffer>("DenoiserData");

          shaderDataBuffer->viewMatrix = passData.viewMatrix;
          shaderDataBuffer->projMatrix = passData.projMatrix;
          shaderDataBuffer->viewportSize = glm::vec4(this->viewportExtent.width, this->viewportExtent.height, 0.0f, 0.0f);
          shaderDataBuffer->radius = (!glfwGetKey(window, GLFW_KEY_A) ? 0 : 2);
        }
        passData.memoryPool->EndSet();

        std::vector<legit::ImageSamplerBinding> imageSamplerBindings;
        imageSamplerBindings.push_back(shaderDataSetInfo->MakeImageSamplerBinding("depthStencilSampler", passContext.GetImageView(this->viewportResources->depthMoments.imageViewProxy->Id()), screenspaceSampler.get()));
        imageSamplerBindings.push_back(shaderDataSetInfo->MakeImageSamplerBinding("normalSampler", passContext.GetImageView(this->viewportResources->normal.imageViewProxy->Id()), screenspaceSampler.get()));
        imageSamplerBindings.push_back(shaderDataSetInfo->MakeImageSamplerBinding("noisySampler", passContext.GetImageView(this->viewportResources->indirectLight.imageViewProxy->Id()), screenspaceSampler.get()));

        auto shaderDataSet = this->core->GetDescriptorSetCache()->GetDescriptorSet(*shaderDataSetInfo, shaderData.uniformBufferBindings, {}, imageSamplerBindings);

        passContext.GetCommandBuffer().bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeineInfo.pipelineLayout, ShaderDataSetIndex, { shaderDataSet }, { shaderData.dynamicOffset });
        passContext.GetCommandBuffer().draw(4, 1, 0, 0);
      }
    }));

    //final gathering
    core->GetRenderGraph()->AddPass(legit::RenderGraph::RenderPassDesc()
      .SetColorAttachments({ frameInfo.swapchainImageViewProxyId })
      .SetInputImages({
        viewportResources->directLight.imageViewProxy->Id(),
        viewportResources->blurredDirectLight.imageViewProxy->Id(),
        viewportResources->albedo.imageViewProxy->Id(),
        viewportResources->denoisedIndirectLight.imageViewProxy->Id()})
      .SetRenderAreaExtent(viewportExtent)
      .SetProfilerInfo(legit::Colors::pomegranate, "GatheringPass")
      .SetRecordFunc([this, passData](legit::RenderGraph::RenderPassContext passContext)
    {
      auto pipeineInfo = this->core->GetPipelineCache()->BindGraphicsPipeline(passContext.GetCommandBuffer(), passContext.GetRenderPass()->GetHandle(), legit::DepthSettings::Disabled(), { legit::BlendSettings::Opaque() }, legit::VertexDeclaration(), vk::PrimitiveTopology::eTriangleFan, finalGathererShader.program.get());
      {
        const legit::DescriptorSetLayoutKey *shaderDataSetInfo = finalGathererShader.fragment->GetSetInfo(ShaderDataSetIndex);
        auto shaderData = passData.memoryPool->BeginSet(shaderDataSetInfo);
        {
          auto shaderDataBuffer = passData.memoryPool->GetUniformBufferData<FinalGathererShader::DataBuffer>("FinalGathererData");

          shaderDataBuffer->viewMatrix = passData.viewMatrix;
          shaderDataBuffer->projMatrix = passData.projMatrix;
        }
        passData.memoryPool->EndSet();

        std::vector<legit::ImageSamplerBinding> imageSamplerBindings;
        auto directLightImageView = passContext.GetImageView(this->viewportResources->directLight.imageViewProxy->Id());
        imageSamplerBindings.push_back(shaderDataSetInfo->MakeImageSamplerBinding("directLightSampler", directLightImageView, screenspaceSampler.get()));
        auto blurredDirectLightImageView = passContext.GetImageView(this->viewportResources->blurredDirectLight.imageViewProxy->Id());
        imageSamplerBindings.push_back(shaderDataSetInfo->MakeImageSamplerBinding("blurredDirectLightSampler", blurredDirectLightImageView, screenspaceSampler.get()));
        auto albedoImageView = passContext.GetImageView(this->viewportResources->albedo.imageViewProxy->Id());
        imageSamplerBindings.push_back(shaderDataSetInfo->MakeImageSamplerBinding("albedoSampler", albedoImageView, screenspaceSampler.get()));
        auto indirectLightImageView = passContext.GetImageView(this->viewportResources->denoisedIndirectLight.imageViewProxy->Id());
        imageSamplerBindings.push_back(shaderDataSetInfo->MakeImageSamplerBinding("indirectLightSampler", indirectLightImageView, screenspaceSampler.get()));
        auto shaderDataSet = this->core->GetDescriptorSetCache()->GetDescriptorSet(*shaderDataSetInfo, shaderData.uniformBufferBindings, {}, imageSamplerBindings);

        passContext.GetCommandBuffer().bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeineInfo.pipelineLayout, ShaderDataSetIndex, { shaderDataSet }, { shaderData.dynamicOffset });
        passContext.GetCommandBuffer().draw(4, 1, 0, 0);
      }
    }));

    std::vector<legit::RenderGraph::ImageViewProxyId> debugProxies;
    //debugProxies = frameResources->blurredDirectLight.mipImageViewProxies;
    debugProxies.push_back(viewportResources->emissive.imageViewProxy->Id());
    debugProxies.push_back(viewportResources->indirectLight.imageViewProxy->Id());
    debugProxies.push_back(viewportResources->denoisedIndirectLight.imageViewProxy->Id());
    debugRenderer.RenderImageViews(core->GetRenderGraph(), frameInfo.memoryPool, frameInfo.swapchainImageViewProxyId, debugProxies);
  }

  void ReloadShaders()
  {
    shadowmapBuilderShader.vertex.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/Common/shadowmapBuilder.vert.spv"));
    shadowmapBuilderShader.fragment.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/Common/shadowmapBuilder.frag.spv"));
    shadowmapBuilderShader.program.reset(new legit::ShaderProgram(shadowmapBuilderShader.vertex.get(), shadowmapBuilderShader.fragment.get()));

    gBufferBuilderShader.vertex.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/Common/gBufferBuilder.vert.spv"));
    gBufferBuilderShader.fragment.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/Common/gBufferBuilderDepth.frag.spv"));
    gBufferBuilderShader.program.reset(new legit::ShaderProgram(gBufferBuilderShader.vertex.get(), gBufferBuilderShader.fragment.get()));

    directLightingShader.vertex.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/Common/screenspaceQuad.vert.spv"));
    directLightingShader.fragment.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/Common/directLighting.frag.spv"));
    directLightingShader.program.reset(new legit::ShaderProgram(directLightingShader.vertex.get(), directLightingShader.fragment.get()));

    indirectLightingShader.vertex.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/Common/screenspaceQuad.vert.spv"));
    indirectLightingShader.fragment.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/SSCTGI/indirectLighting.frag.spv"));
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

  struct ViewportResources
  {
    ViewportResources(legit::RenderGraph *renderGraph, glm::uvec2 screenSize) :
      albedo(renderGraph, vk::Format::eR16G16B16A16Sfloat, screenSize, legit::colorImageUsage),
      emissive(renderGraph, vk::Format::eR16G16B16A16Sfloat, screenSize, legit::colorImageUsage),
      normal(renderGraph, vk::Format::eR16G16B16A16Sfloat, screenSize, legit::colorImageUsage),
      depthMoments(renderGraph, vk::Format::eR32G32B32A32Sfloat, screenSize, legit::colorImageUsage),
      blurredDepthMoments(renderGraph, vk::Format::eR32G32B32A32Sfloat, screenSize, legit::colorImageUsage),
      depthStencil(renderGraph, vk::Format::eD32Sfloat, screenSize, legit::depthImageUsage),
      directLight(renderGraph, vk::Format::eR16G16B16A16Sfloat, screenSize, legit::colorImageUsage),
      blurredDirectLight(renderGraph, vk::Format::eR16G16B16A16Sfloat, screenSize, legit::colorImageUsage),
      shadowMap(renderGraph, vk::Format::eD32Sfloat, glm::uvec2(1024, 1024), legit::colorImageUsage),
      indirectLight(renderGraph, vk::Format::eR16G16B16A16Sfloat, screenSize, legit::colorImageUsage),
      denoisedIndirectLight(renderGraph, vk::Format::eR16G16B16A16Sfloat, screenSize, legit::colorImageUsage)
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
  std::unique_ptr<ViewportResources> viewportResources;

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

  vk::Extent2D viewportExtent;
  MipBuilder mipBuilder;
  BlurBuilder blurBuilder;
  DebugRenderer debugRenderer;

  std::unique_ptr<legit::Sampler> screenspaceSampler;
  std::unique_ptr<legit::Sampler> shadowmapSampler;

  legit::Core *core;

};