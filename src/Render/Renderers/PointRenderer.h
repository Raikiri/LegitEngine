#include "../Common/MipBuilder.h"
#include "../Common/BlurBuilder.h"
#include "../Common/ArrayBucketeer.h"
#include "../Common/ListBucketeer.h"
#include "../Common/DebugRenderer.h"


class PointRenderer : public BaseRenderer
{
public:
  PointRenderer(legit::Core *_core) :
    mipBuilder(_core),
    blurBuilder(_core),
    debugRenderer(_core),
    arrayBucketeer(_core),
    listBucketeer(_core),
    giBucketeer(_core),
    directLightBucketeer(_core)
  {
    this->core = _core;

    vertexDecl = Mesh::GetVertexDeclaration();

    screenspaceSampler.reset(new legit::Sampler(core->GetLogicalDevice(), vk::SamplerAddressMode::eClampToEdge, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear));
    shadowmapSampler.reset(new legit::Sampler(core->GetLogicalDevice(), vk::SamplerAddressMode::eClampToEdge, vk::Filter::eLinear, vk::SamplerMipmapMode::eNearest, true));
    ReloadShaders();

    {
      auto texelData = legit::LoadKtxFromFile("../data/Textures/DiffBrushesPremult.ktx");
      //auto texelData = legit::LoadKtxFromFile("../data/Textures/BrushesOverlay.ktx");
      //auto texelData = legit::LoadCubemapFromFile("../data/Textures/Cubemaps/3_16f.ktx");
      //auto texelData = legit::LoadCubemapFromFile("../data/Textures/Cubemaps/5_16f.ktx");
      auto texCreateDesc = legit::Image::CreateInfo2d(texelData.baseSize, uint32_t(texelData.mips.size()), uint32_t(texelData.layersCount), texelData.format, vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst);
      this->brushImage = std::unique_ptr<legit::Image>(new legit::Image(core->GetPhysicalDevice(), core->GetLogicalDevice(), texCreateDesc));
      legit::LoadTexelData(core, &texelData, brushImage->GetImageData());
      this->brushImageView = std::unique_ptr<legit::ImageView>(new legit::ImageView(core->GetLogicalDevice(), brushImage->GetImageData(), 0, brushImage->GetImageData()->GetMipsCount(), 0, 1));
    }
    useArrayBuckets = false;
    useBlockGathering = true;
    useSizedGathering = true;

    debugMip = -1;
    debugType = -1;
  }

  void RecreateSceneResources(Scene *scene)
  {
    size_t pointsCount = 0;
    scene->IterateObjects([&](glm::mat4 objectToWorld, glm::vec3 albedoColor, glm::vec3 emissiveColor, vk::Buffer vertexBuffer, vk::Buffer indexBuffer, uint32_t verticesCount, uint32_t indicesCount)
    {
      pointsCount += verticesCount;
    });


    sceneResources.reset(new SceneResources(core, pointsCount));
    listBucketeer.RecreateSceneResources(pointsCount);
    giBucketeer.RecreateSceneResources(pointsCount);
    directLightBucketeer.RecreateSceneResources(pointsCount);
    arrayBucketeer.RecreateSceneResources(pointsCount);
  }
  
  void RecreateSwapchainResources(vk::Extent2D viewportExtent, size_t framesInFlightCount)
  {
    this->viewportExtent = viewportExtent;

    glm::uvec2 viewportSize = { viewportExtent.width, viewportExtent.height };
    viewportResources.reset(new ViewportResources(core->GetRenderGraph(), viewportSize));

    listBucketeer.RecreateSwapchainResources(glm::uvec2(512, 512), framesInFlightCount);
    this->giViewportSize = glm::uvec2(128, 128);
    giBucketeer.RecreateSwapchainResources(giViewportSize, framesInFlightCount);
    directLightBucketeer.RecreateSwapchainResources(glm::uvec2(64, 64), framesInFlightCount);
    arrayBucketeer.RecreateSwapchainResources(glm::uvec2(512, 512), framesInFlightCount);
  }
private:
  #pragma pack(push, 1)
  struct DrawCallDataBuffer
  {
    glm::mat4 modelMatrix;
    glm::vec4 albedoColor;
    glm::vec4 emissiveColor;
    int basePointIndex;
  };
  #pragma pack(pop)
  
  struct FrameResources;
  struct PassData
  {
    legit::ShaderMemoryPool *memoryPool;
    //legit::RenderGraph::ImageViewProxyId swapchainImageViewProxyId;
    vk::Extent2D shadowmapViewportSize;
    glm::mat4 viewMatrix;
    glm::mat4 projMatrix;
    glm::mat4 lightViewMatrix;
    glm::mat4 lightProjMatrix;
    glm::mat4 bucketViewMatrix;
    glm::mat4 bucketProjMatrix;
    float fovy;
    float pointWorldSize;
    int frameIndex;
    Scene *scene;
  };

  void RasterizeIndexPyramid(legit::RenderGraph *renderGraph, const MippedProxy &indexPyramid, legit::RenderGraph::ImageViewProxyId depthStencilProxyId, glm::mat4 projMatrix, glm::mat4 viewMatrix, PassData passData)
  {
    vk::Extent2D viewportSize(indexPyramid.baseSize.x, indexPyramid.baseSize.y);

    renderGraph->AddPass(legit::RenderGraph::RenderPassDesc()
      .SetColorAttachments({ 
        { indexPyramid.mipImageViewProxies[0]->Id(), vk::AttachmentLoadOp::eClear, vk::ClearColorValue(std::array<int32_t, 4>{0, 0, 0, 0})} })
      .SetDepthAttachment(depthStencilProxyId, vk::AttachmentLoadOp::eClear)
      .SetStorageBuffers({
        this->sceneResources->pointData->Id()})
      .SetRenderAreaExtent(viewportSize)
      .SetProfilerInfo(legit::Colors::carrot, "PassPointRaster")
      .SetRecordFunc([this, passData, viewportSize, projMatrix, viewMatrix](legit::RenderGraph::RenderPassContext passContext)
    {
      std::vector<legit::BlendSettings> attachmentBlendSettings;
      attachmentBlendSettings.resize(passContext.GetRenderPass()->GetColorAttachmentsCount(), legit::BlendSettings::Opaque());
      auto shaderProgram = pointRasterizerShader.program.get();
      auto pipeineInfo = this->core->GetPipelineCache()->BindGraphicsPipeline(passContext.GetCommandBuffer(), passContext.GetRenderPass()->GetHandle(), legit::DepthSettings::DepthTest(), attachmentBlendSettings, vertexDecl, vk::PrimitiveTopology::ePointList, shaderProgram);
      {
        const legit::DescriptorSetLayoutKey *shaderDataSetInfo = shaderProgram->GetSetInfo(ShaderDataSetIndex);
        auto shaderData = passData.memoryPool->BeginSet(shaderDataSetInfo);
        {
          auto shaderDataBuffer = passData.memoryPool->GetUniformBufferData<PointRasterizerShader::DataBuffer>("PointRasterizerData");

          shaderDataBuffer->projMatrix = projMatrix;
          shaderDataBuffer->viewMatrix = viewMatrix;
          shaderDataBuffer->viewportSize = glm::vec4(viewportSize.width, viewportSize.height, 0.0f, 0.0f);
          shaderDataBuffer->time = 0.0f;
        }
        passData.memoryPool->EndSet();

        std::vector<legit::StorageBufferBinding> storageBufferBindings;
        auto pointDataBuffer = passContext.GetBuffer(this->sceneResources->pointData->Id());
        storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("PointsBuffer", pointDataBuffer));

        auto shaderDataSet = this->core->GetDescriptorSetCache()->GetDescriptorSet(*shaderDataSetInfo, shaderData.uniformBufferBindings, storageBufferBindings, {});

        const legit::DescriptorSetLayoutKey *drawCallSetInfo = shaderProgram->GetSetInfo(DrawCallDataSetIndex);

        int basePointIndex = 0;
        passData.scene->IterateObjects([&](glm::mat4 objectToWorld, glm::vec3 albedoColor, glm::vec3 emissiveColor, vk::Buffer vertexBuffer , vk::Buffer indexBuffer, uint32_t verticesCount, uint32_t indicesCount)
        {
          auto drawCallData = passData.memoryPool->BeginSet(drawCallSetInfo);
          {
            auto drawCallData = passData.memoryPool->GetUniformBufferData<DrawCallDataBuffer>("DrawCallData");
            drawCallData->modelMatrix = objectToWorld;
            drawCallData->albedoColor = glm::vec4(albedoColor, 1.0f);
            drawCallData->emissiveColor = glm::vec4(emissiveColor, 1.0f);
            drawCallData->basePointIndex = basePointIndex;
          }
          passData.memoryPool->EndSet();
          basePointIndex += verticesCount;

          auto drawCallSet = this->core->GetDescriptorSetCache()->GetDescriptorSet(*drawCallSetInfo, drawCallData.uniformBufferBindings, {}, {});
          passContext.GetCommandBuffer().bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeineInfo.pipelineLayout, ShaderDataSetIndex,
            { shaderDataSet, drawCallSet },
            { shaderData.dynamicOffset, drawCallData.dynamicOffset });

          passContext.GetCommandBuffer().bindVertexBuffers(0, { vertexBuffer }, { 0 });
          passContext.GetCommandBuffer().draw(verticesCount, 1, 0, 0);
        });
      }
    }));

    vk::Extent2D prevLevelViewportSize = viewportSize;
    for (size_t level = 1; level < indexPyramid.mipImageViewProxies.size(); level++)
    {
      
      auto prevLevelViewProxyId = indexPyramid.mipImageViewProxies[level - 1]->Id();
      auto currLevelViewProxyId = indexPyramid.mipImageViewProxies[level]->Id();

      vk::Extent2D currLevelViewportSize = prevLevelViewportSize;
      currLevelViewportSize.width /= 2;
      currLevelViewportSize.height /= 2;

      renderGraph->AddPass(legit::RenderGraph::RenderPassDesc()
        .SetColorAttachments({
          { currLevelViewProxyId, vk::AttachmentLoadOp::eDontCare } })
        .SetInputImages({
          prevLevelViewProxyId})
        .SetStorageBuffers({
          this->sceneResources->pointData->Id() })
        .SetRenderAreaExtent(currLevelViewportSize)
        .SetProfilerInfo(legit::Colors::nephritis, "PassMipBuilder")
        .SetRecordFunc([this, passData, viewMatrix, projMatrix, prevLevelViewProxyId, prevLevelViewportSize, currLevelViewportSize](legit::RenderGraph::RenderPassContext passContext)
      {
        auto shaderProgram = mipBuilderShader.program.get();
        auto pipeineInfo = this->core->GetPipelineCache()->BindGraphicsPipeline(passContext.GetCommandBuffer(), passContext.GetRenderPass()->GetHandle(), legit::DepthSettings::Disabled(), { legit::BlendSettings::Opaque() }, legit::VertexDeclaration(), vk::PrimitiveTopology::eTriangleFan, shaderProgram);
        {
          const legit::DescriptorSetLayoutKey *shaderDataSetInfo = shaderProgram->GetSetInfo(ShaderDataSetIndex);
          auto shaderData = passData.memoryPool->BeginSet(shaderDataSetInfo);
          {
            auto shaderDataBuffer = passData.memoryPool->GetUniformBufferData<MipBuilderShader::DataBuffer>("MipBuilderData");

            shaderDataBuffer->viewMatrix = viewMatrix;
            shaderDataBuffer->projMatrix = projMatrix;
            shaderDataBuffer->prevViewportSize = glm::vec4(prevLevelViewportSize.width, prevLevelViewportSize.height, 0.0f, 0.0f);
            shaderDataBuffer->currViewportSize = glm::vec4(currLevelViewportSize.width, currLevelViewportSize.height, 0.0f, 0.0f);
            shaderDataBuffer->time = 0.0f;
          }
          passData.memoryPool->EndSet();

          std::vector<legit::ImageSamplerBinding> imageSamplerBindings;
          auto prevIndicesImageView = passContext.GetImageView(prevLevelViewProxyId);
          imageSamplerBindings.push_back(shaderDataSetInfo->MakeImageSamplerBinding("prevIndicesSampler", prevIndicesImageView, screenspaceSampler.get()));

          std::vector<legit::StorageBufferBinding> storageBufferBindings;
          auto pointDataBuffer = passContext.GetBuffer(this->sceneResources->pointData->Id());
          storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("PointsBuffer", pointDataBuffer));

          auto shaderDataSet = this->core->GetDescriptorSetCache()->GetDescriptorSet(*shaderDataSetInfo, shaderData.uniformBufferBindings, storageBufferBindings, imageSamplerBindings);
          passContext.GetCommandBuffer().bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeineInfo.pipelineLayout, ShaderDataSetIndex, { shaderDataSet }, { shaderData.dynamicOffset });
          passContext.GetCommandBuffer().draw(4, 1, 0, 0);
        }
      }));
      prevLevelViewportSize = currLevelViewportSize;
    }
  }

  void JumpFillIndexPyramid(legit::RenderGraph *renderGraph, legit::RenderGraph::ImageViewProxyId indexPyramid[2], vk::Extent2D viewportSize, glm::mat4 projMatrix, glm::mat4 viewMatrix, PassData passData, size_t jumpsCount)
  {
    for(size_t jumpIndex = 0; jumpIndex < jumpsCount; jumpIndex++)
    {
      auto srcIndexProxyId = indexPyramid[jumpIndex % 2];
      auto dstIndexProxyId = indexPyramid[(jumpIndex + 1) % 2];
      //direct light splatting
      renderGraph->AddPass(legit::RenderGraph::RenderPassDesc()
        .SetColorAttachments({
          {dstIndexProxyId, vk::AttachmentLoadOp::eDontCare } })
        .SetInputImages({
          srcIndexProxyId })
        .SetStorageBuffers({
          this->sceneResources->pointData->Id() })
        .SetRenderAreaExtent(viewportSize)
        .SetProfilerInfo(legit::Colors::wisteria, "PassJumpFill")
        .SetRecordFunc([this, passData, srcIndexProxyId, viewportSize, projMatrix, viewMatrix, jumpIndex, jumpsCount](legit::RenderGraph::RenderPassContext passContext)
      {
        auto shaderProgram = jumpFillShader.program.get();
        auto pipeineInfo = this->core->GetPipelineCache()->BindGraphicsPipeline(passContext.GetCommandBuffer(), passContext.GetRenderPass()->GetHandle(), legit::DepthSettings::Disabled(), { legit::BlendSettings::Opaque() }, legit::VertexDeclaration(), vk::PrimitiveTopology::eTriangleFan, shaderProgram);
        {
          const legit::DescriptorSetLayoutKey *shaderDataSetInfo = shaderProgram->GetSetInfo(ShaderDataSetIndex);
          auto shaderData = passData.memoryPool->BeginSet(shaderDataSetInfo);
          {
            auto shaderDataBuffer = passData.memoryPool->GetUniformBufferData<JumpFillShader::DataBuffer>("JumpFillData");

            shaderDataBuffer->viewMatrix = viewMatrix;
            shaderDataBuffer->projMatrix = projMatrix;
            shaderDataBuffer->viewportSize = glm::vec4(viewportSize.width, viewportSize.height, 0.0f, 0.0f);
            shaderDataBuffer->time = 0.0f;
            shaderDataBuffer->jumpDist = 1 << (int(jumpsCount) - jumpIndex - 1);
          }
          passData.memoryPool->EndSet();

          std::vector<legit::ImageSamplerBinding> imageSamplerBindings;
          auto pointIndicesImageView = passContext.GetImageView(srcIndexProxyId);
          imageSamplerBindings.push_back(shaderDataSetInfo->MakeImageSamplerBinding("pointIndicesSampler", pointIndicesImageView, screenspaceSampler.get()));
          imageSamplerBindings.push_back(shaderDataSetInfo->MakeImageSamplerBinding("brushSampler", brushImageView.get(), screenspaceSampler.get()));

          std::vector<legit::StorageBufferBinding> storageBufferBindings;
          auto pointDataBuffer = passContext.GetBuffer(this->sceneResources->pointData->Id());
          storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("PointsBuffer", pointDataBuffer));


          auto shaderDataSet = this->core->GetDescriptorSetCache()->GetDescriptorSet(*shaderDataSetInfo, shaderData.uniformBufferBindings, storageBufferBindings, imageSamplerBindings);
          passContext.GetCommandBuffer().bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeineInfo.pipelineLayout, ShaderDataSetIndex, { shaderDataSet }, { shaderData.dynamicOffset });
          passContext.GetCommandBuffer().draw(4, 1, 0, 0);
        }
      }));
    }
  }


  public:
  void RenderFrame(const legit::InFlightQueue::FrameInfo &frameInfo, const Camera &camera, const Camera &light, Scene *scene, GLFWwindow *window)
  {
    ImGui::Begin("Point renderer stuff");

    static float ang = 0.0f;
    Camera bucketPos;
    bucketPos.horAngle = 2.0f * 3.1415f * dis(eng);
    bucketPos.vertAngle = asin(std::max(-1.0f, std::min(1.0f, 2.0f * dis(eng) - 1.0f)));
    ang += 0.01f;

    PassData passData;

    passData.memoryPool = frameInfo.memoryPool;
    passData.shadowmapViewportSize = vk::Extent2D(viewportResources->shadowmapIndexPyramid.baseSize.x, viewportResources->shadowmapIndexPyramid.baseSize.y);
    passData.scene = scene;
    passData.viewMatrix = glm::inverse(camera.GetTransformMatrix());
    passData.lightViewMatrix = glm::inverse(light.GetTransformMatrix());
    passData.bucketViewMatrix = glm::inverse(bucketPos.GetTransformMatrix());
    passData.fovy = 1.0f;
    passData.pointWorldSize = 0.1f;
    //passData.swapchainImageViewProxyId = frameInfo.swapchainImageViewProxyId;
    float aspect = float(this->viewportExtent.width) / float(this->viewportExtent.height);
    passData.projMatrix = glm::perspectiveZO(passData.fovy, aspect, 0.01f, 1000.0f) * glm::scale(glm::vec3(1.0f, -1.0f, -1.0f));
    passData.lightProjMatrix = glm::perspectiveZO(0.8f, 1.0f, 0.1f, 100.0f) * glm::scale(glm::vec3(1.0f, -1.0f, -1.0f));
    float bucketSize = 8.0f;
    passData.bucketProjMatrix = glm::orthoZO(-bucketSize, bucketSize, -bucketSize, bucketSize, -bucketSize, bucketSize) * glm::scale(glm::vec3(1.0f, -1.0f, -1.0f));
    static uint32_t frameIndex = 0;
    passData.frameIndex = frameIndex;
    frameIndex++;

    //point rasterizer
    /*RasterizeIndexPyramid(
      core->GetRenderGraph(),
      viewportResources->indexPyramidPing,
      viewportResources->depthStencil.imageViewProxy->Id(),
      passData.projMatrix,
      passData.viewMatrix,
      passData);
    legit::RenderGraph::ImageViewProxyId indexTextures[] = { viewportResources->indexPyramidPing.mipImageViewProxies[0]->Id(), viewportResources->indexPyramidPong.mipImageViewProxies[0]->Id() };

    JumpFillIndexPyramid(
      core->GetRenderGraph(),
      indexTextures,
      this->viewportExtent,
      passData.projMatrix,
      passData.viewMatrix,
      passData,
      6);*/

    /*core->GetRenderGraph()->AddPass(legit::RenderGraph::RenderPassDesc()
      .SetColorAttachments({ 
        { viewportResources->indexPyramidPing.mipImageViewProxies[0]->Id(), vk::AttachmentLoadOp::eClear, vk::ClearColorValue(std::array<int32_t, 4>{0, 0, 0, 0})} })
      .SetDepthAttachment(viewportResources->depthStencil.imageViewProxy->Id(), vk::AttachmentLoadOp::eClear)
      .SetStorageBuffers({
        this->sceneResources->pointData->Id()})
      .SetRenderAreaExtent(this->viewportExtent)
      .SetProfilerInfo(legit::Colors::carrot, "PassPointSprites")
      .SetRecordFunc([this, passData](legit::RenderGraph::RenderPassContext passContext)
    {
      std::vector<legit::BlendSettings> attachmentBlendSettings;
      attachmentBlendSettings.resize(passContext.GetRenderPass()->GetColorAttachmentsCount(), legit::BlendSettings::Opaque());
      auto shaderProgram = pointSpritesShader.program.get();
      auto pipeineInfo = this->core->GetPipelineCache()->BindGraphicsPipeline(passContext.GetCommandBuffer(), passContext.GetRenderPass()->GetHandle(), legit::DepthSettings::DepthTest(), attachmentBlendSettings, vertexDecl, vk::PrimitiveTopology::ePointList, shaderProgram);
      {
        const legit::DescriptorSetLayoutKey *shaderDataSetInfo = shaderProgram->GetSetInfo(ShaderDataSetIndex);
        auto shaderData = passData.memoryPool->BeginSet(shaderDataSetInfo);
        {
          auto shaderDataBuffer = passData.memoryPool->GetUniformBufferData<PointSpritesShader::DataBuffer>("PassData");

          shaderDataBuffer->projMatrix = passData.projMatrix;
          shaderDataBuffer->viewMatrix = passData.viewMatrix;
          shaderDataBuffer->viewportSize = glm::vec4(this->viewportExtent.width, this->viewportExtent.height, 0.0f, 0.0f);
          shaderDataBuffer->fovy = passData.fovy;
          shaderDataBuffer->pointWorldSize = passData.pointWorldSize;
          shaderDataBuffer->time = 0.0f;
        }
        passData.memoryPool->EndSet();

        std::vector<legit::StorageBufferBinding> storageBufferBindings;
        auto pointDataBuffer = passContext.GetBuffer(this->sceneResources->pointData->Id());
        storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("PointsBuffer", pointDataBuffer));

        std::vector<legit::ImageSamplerBinding> imageSamplerBindings;
        imageSamplerBindings.push_back(shaderDataSetInfo->MakeImageSamplerBinding("brushSampler", brushImageView.get(), screenspaceSampler.get()));


        auto shaderDataSet = this->core->GetDescriptorSetCache()->GetDescriptorSet(*shaderDataSetInfo, shaderData.uniformBufferBindings, storageBufferBindings, imageSamplerBindings);

        const legit::DescriptorSetLayoutKey *drawCallSetInfo = shaderProgram->GetSetInfo(DrawCallDataSetIndex);

        int basePointIndex = 0;
        passData.scene->IterateObjects([&](glm::mat4 objectToWorld, glm::vec3 albedoColor, glm::vec3 emissiveColor, vk::Buffer vertexBuffer , vk::Buffer indexBuffer, uint32_t verticesCount, uint32_t indicesCount)
        {
          auto drawCallData = passData.memoryPool->BeginSet(drawCallSetInfo);
          {
            auto drawCallData = passData.memoryPool->GetUniformBufferData<DrawCallDataBuffer>("DrawCallData");
            drawCallData->modelMatrix = objectToWorld;
            drawCallData->albedoColor = glm::vec4(albedoColor, 1.0f);
            drawCallData->emissiveColor = glm::vec4(emissiveColor, 1.0f);
            drawCallData->basePointIndex = basePointIndex;
          }
          passData.memoryPool->EndSet();
          basePointIndex += verticesCount;

          auto drawCallSet = this->core->GetDescriptorSetCache()->GetDescriptorSet(*drawCallSetInfo, drawCallData.uniformBufferBindings, {}, {});
          passContext.GetCommandBuffer().bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeineInfo.pipelineLayout, ShaderDataSetIndex,
            { shaderDataSet, drawCallSet },
            { shaderData.dynamicOffset, drawCallData.dynamicOffset });

          passContext.GetCommandBuffer().bindVertexBuffers(0, { vertexBuffer }, { 0 });
          passContext.GetCommandBuffer().draw(verticesCount, 1, 0, 0);
        });
      }
    }));*/

    //direct light point indices
    RasterizeIndexPyramid(
      core->GetRenderGraph(), 
      viewportResources->shadowmapIndexPyramid,
      viewportResources->shadowmapDepthStencil.imageViewProxy->Id(), 
      passData.lightProjMatrix, 
      passData.lightViewMatrix, 
      passData);

    //direct light splatting
    /*core->GetRenderGraph()->AddPass(legit::RenderGraph::RenderPassDesc()
      .SetColorAttachments({
        {viewportResources->shadowmapDebug.imageViewProxy->Id(), vk::AttachmentLoadOp::eDontCare } })
      .SetInputImages({
        viewportResources->shadowmapIndexPyramid.imageViewProxy->Id(),
        viewportResources->shadowmapDepthStencil.imageViewProxy->Id()})
      .SetStorageBuffers({
        sceneResources->pointData->Id()})
      .SetRenderAreaExtent(passData.shadowmapViewportSize)
      .SetProfilerInfo(legit::Colors::sunFlower, "PassDirectLight")
      .SetRecordFunc([this, passData](legit::RenderGraph::RenderPassContext passContext)
    {
      auto shaderProgram = directLightShader.program.get();
      auto pipeineInfo = this->core->GetPipelineCache()->BindGraphicsPipeline(passContext.GetCommandBuffer(), passContext.GetRenderPass()->GetHandle(), legit::DepthSettings::Disabled(), { legit::BlendSettings::Opaque() }, legit::VertexDeclaration(), vk::PrimitiveTopology::eTriangleFan, shaderProgram);
      {
        const legit::DescriptorSetLayoutKey *shaderDataSetInfo = shaderProgram->GetSetInfo(ShaderDataSetIndex);
        auto shaderData = passData.memoryPool->BeginSet(shaderDataSetInfo);
        {
          auto shaderDataBuffer = passData.memoryPool->GetUniformBufferData<DirectLightShader::DataBuffer>("SplattingData");

          shaderDataBuffer->lightViewMatrix = passData.lightViewMatrix;
          shaderDataBuffer->lightProjMatrix = passData.lightProjMatrix;
          shaderDataBuffer->lightViewportSize = glm::vec4(passData.shadowmapViewportSize.width, passData.shadowmapViewportSize.height, 0.0f, 0.0f);
          shaderDataBuffer->time = 0.0f;
        }
        passData.memoryPool->EndSet();

        std::vector<legit::ImageSamplerBinding> imageSamplerBindings;
        auto pointIndicesImageView = passContext.GetImageView(this->viewportResources->shadowmapIndexPyramid.imageViewProxy->Id());
        imageSamplerBindings.push_back(shaderDataSetInfo->MakeImageSamplerBinding("pointIndicesSampler", pointIndicesImageView, screenspaceSampler.get()));

        std::vector<legit::StorageBufferBinding> storageBufferBindings;
        auto pointDataBuffer = passContext.GetBuffer(this->sceneResources->pointData->Id());
        storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("PointsBuffer", pointDataBuffer));

        auto shaderDataSet = this->core->GetDescriptorSetCache()->GetDescriptorSet(*shaderDataSetInfo, shaderData.uniformBufferBindings, storageBufferBindings, imageSamplerBindings);
        passContext.GetCommandBuffer().bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeineInfo.pipelineLayout, ShaderDataSetIndex, { shaderDataSet }, { shaderData.dynamicOffset });
        passContext.GetCommandBuffer().draw(4, 1, 0, 0);
      }
    }));*/




    //splatting
    /*core->GetRenderGraph()->AddPass( legit::RenderGraph::RenderPassDesc()
      .SetColorAttachments({
        {frameInfo.swapchainImageViewProxyId, vk::AttachmentLoadOp::eDontCare } })
      .SetInputImages({
        viewportResources->indexPyramid.imageViewProxyId,
        viewportResources->depthStencil.imageViewProxy->Id()})
      .SetStorageBuffers({
        sceneResources->pointData->Id()})
      .SetRenderAreaExtent(this->viewportExtent)
      .SetRecordFunc([this, passData](legit::RenderGraph::RenderPassContext passContext)
    {
      auto pipeineInfo = this->core->GetPipelineCache()->BindGraphicsPipeline(passContext.GetCommandBuffer(), passContext.GetRenderPass()->GetHandle(), legit::DepthSettings::Disabled(), { legit::BlendSettings::Opaque() }, legit::VertexDeclaration(), vk::PrimitiveTopology::eTriangleFan, splattingShader.program.get());
      {
        const legit::DescriptorSetLayoutKey *shaderDataSetInfo = splattingShader.fragment->GetSetInfo(ShaderDataSetIndex);
        auto shaderData = passData.memoryPool->BeginSet(shaderDataSetInfo);
        {
          auto shaderDataBuffer = passData.memoryPool->GetUniformBufferData<SplattingShader::DataBuffer>("SplattingData");

          shaderDataBuffer->viewMatrix = passData.viewMatrix;
          shaderDataBuffer->projMatrix = passData.projMatrix;
          shaderDataBuffer->viewportSize = glm::vec4(this->viewportExtent.width, this->viewportExtent.height, 0.0f, 0.0f);
          shaderDataBuffer->time = 0.0f;
        }
        passData.memoryPool->EndSet();

        std::vector<legit::ImageSamplerBinding> imageSamplerBindings;
        auto pointIndicesImageView = passContext.GetImageView(this->viewportResources->indexPyramid.imageViewProxyId);
        imageSamplerBindings.push_back(shaderDataSetInfo->MakeImageSamplerBinding("pointIndicesSampler", pointIndicesImageView, screenspaceSampler.get()));
        imageSamplerBindings.push_back(shaderDataSetInfo->MakeImageSamplerBinding("brushSampler", brushImageView.get(), screenspaceSampler.get()));

        std::vector<legit::StorageBufferBinding> storageBufferBindings;
        auto pointDataBuffer = passContext.GetBuffer(this->sceneResources->pointData->Id());
        storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("PointsBuffer", pointDataBuffer));

        auto shaderDataSet = this->core->GetDescriptorSetCache()->GetDescriptorSet(*shaderDataSetInfo, shaderData.uniformBufferBindings, storageBufferBindings, imageSamplerBindings);
        passContext.GetCommandBuffer().bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeineInfo.pipelineLayout, ShaderDataSetIndex, { shaderDataSet }, { shaderData.dynamicOffset });
        passContext.GetCommandBuffer().draw(4, 1, 0, 0);
      }
    }));*/
    if(0)
    {
      auto res = directLightBucketeer.BucketPoints(frameInfo.memoryPool, passData.lightProjMatrix, passData.lightViewMatrix, this->sceneResources->pointData->Id(), uint32_t(sceneResources->pointsCount), true);

      //direct light casting
      core->GetRenderGraph()->AddPass(legit::RenderGraph::ComputePassDesc()
        .SetStorageBuffers({
          this->sceneResources->pointData->Id(),
          res.bucketsProxyId,
          res.mipInfosProxyId,
          res.pointsListProxyId })
        .SetProfilerInfo(legit::Colors::pumpkin, "PassDirectLight")
        .SetRecordFunc([this, res, passData](legit::RenderGraph::PassContext passContext)
      {
        std::vector<legit::BlendSettings> attachmentBlendSettings;
        auto shader = listDirectLightShader.compute.get();
        auto pipeineInfo = this->core->GetPipelineCache()->BindComputePipeline(passContext.GetCommandBuffer(), shader);
        {
          const legit::DescriptorSetLayoutKey *shaderDataSetInfo = shader->GetSetInfo(ShaderDataSetIndex);
          auto shaderData = passData.memoryPool->BeginSet(shaderDataSetInfo);
          {
            auto shaderDataBuffer = passData.memoryPool->GetUniformBufferData<ListDirectLightShader::DataBuffer>("PassDataBuffer");

            shaderDataBuffer->lightProjMatrix = passData.lightProjMatrix;
            shaderDataBuffer->lightViewMatrix = passData.lightViewMatrix;
            shaderDataBuffer->lightViewportSize = glm::vec4(passData.shadowmapViewportSize.width, passData.shadowmapViewportSize.height, 0.0f, 0.0f);
            shaderDataBuffer->time = 0.0f;
            shaderDataBuffer->totalBucketsCount = glm::uint(res.totalBucketsCount);
          }
          passData.memoryPool->EndSet();

          std::vector<legit::StorageBufferBinding> storageBufferBindings;
          auto bucketsBuffer = passContext.GetBuffer(res.bucketsProxyId);
          storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("BucketsBuffer", bucketsBuffer));

          auto mipInfosBuffer = passContext.GetBuffer(res.mipInfosProxyId);
          storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("MipInfosBuffer", mipInfosBuffer));

          auto pointsListBuffer = passContext.GetBuffer(res.pointsListProxyId);
          storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("PointsListBuffer", pointsListBuffer));

          auto pointsBuffer = passContext.GetBuffer(this->sceneResources->pointData->Id());
          storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("PointsBuffer", pointsBuffer));

          auto shaderDataSet = this->core->GetDescriptorSetCache()->GetDescriptorSet(*shaderDataSetInfo, shaderData.uniformBufferBindings, storageBufferBindings, {});
          passContext.GetCommandBuffer().bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipeineInfo.pipelineLayout, ShaderDataSetIndex, { shaderDataSet }, { shaderData.dynamicOffset });

          glm::uvec3 workGroupSize = shader->GetLocalSize();
          passContext.GetCommandBuffer().dispatch(uint32_t(res.totalBucketsCount) / workGroupSize.x + 1, 1, 1);
        }
      }));
    }

    {
      auto res = giBucketeer.BucketPoints(frameInfo.memoryPool, passData.bucketProjMatrix, passData.bucketViewMatrix, this->sceneResources->pointData->Id(), uint32_t(sceneResources->pointsCount), true);

      //bucket casting
      /*core->GetRenderGraph()->AddPass(legit::RenderGraph::ComputePassDesc()
        .SetStorageBuffers({
          this->sceneResources->pointData->Id(),
          res.bucketsProxyId,
          res.mipInfosProxyId,
          res.pointsListProxyId })
        .SetProfilerInfo(legit::Colors::pumpkin, "PassBucketCasting")
        .SetRecordFunc([this, res, passData](legit::RenderGraph::PassContext passContext)
      {
        std::vector<legit::BlendSettings> attachmentBlendSettings;
        auto shader = listBucketCastShader.compute.get();
        auto pipeineInfo = this->core->GetPipelineCache()->BindComputePipeline(passContext.GetCommandBuffer(), shader);
        {
          const legit::DescriptorSetLayoutKey *shaderDataSetInfo = shader->GetSetInfo(ShaderDataSetIndex);
          auto shaderData = passData.memoryPool->BeginSet(shaderDataSetInfo);
          {
            auto shaderDataBuffer = passData.memoryPool->GetUniformBufferData<ListBucketCastShader::DataBuffer>("BucketCastData");

            shaderDataBuffer->projMatrix = passData.bucketProjMatrix;
            shaderDataBuffer->viewMatrix = passData.bucketViewMatrix;
            shaderDataBuffer->totalBucketsCount = glm::uint(res.totalBucketsCount);
            shaderDataBuffer->time = 0.0f;
            shaderDataBuffer->frameIndex = passData.frameIndex;
          }
          passData.memoryPool->EndSet();

          std::vector<legit::StorageBufferBinding> storageBufferBindings;
          auto bucketsBuffer = passContext.GetBuffer(res.bucketsProxyId);
          storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("BucketsBuffer", bucketsBuffer));

          auto mipInfosBuffer = passContext.GetBuffer(res.mipInfosProxyId);
          storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("MipInfosBuffer", mipInfosBuffer));

          auto pointsListBuffer = passContext.GetBuffer(res.pointsListProxyId);
          storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("PointsListBuffer", pointsListBuffer));

          auto pointsBuffer = passContext.GetBuffer(this->sceneResources->pointData->Id());
          storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("PointsBuffer", pointsBuffer));

          auto shaderDataSet = this->core->GetDescriptorSetCache()->GetDescriptorSet(*shaderDataSetInfo, shaderData.uniformBufferBindings, storageBufferBindings, {});
          passContext.GetCommandBuffer().bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipeineInfo.pipelineLayout, ShaderDataSetIndex, { shaderDataSet }, { shaderData.dynamicOffset });

          glm::uvec3 workGroupSize = shader->GetLocalSize();
          passContext.GetCommandBuffer().dispatch(uint32_t(res.totalBucketsCount / workGroupSize.x + 1), 1, 1);
        }
      }));*/
      core->GetRenderGraph()->AddPass( legit::RenderGraph::RenderPassDesc()
        .SetColorAttachments({
          {frameInfo.swapchainImageViewProxyId, vk::AttachmentLoadOp::eDontCare } })
        .SetStorageBuffers({
          this->sceneResources->pointData->Id(),
          res.bucketsProxyId,
          res.mipInfosProxyId,
          res.pointsListProxyId,
          res.blockPointsListProxyId })
        .SetRenderAreaExtent(vk::Extent2D(giViewportSize.x, giViewportSize.y))
        .SetProfilerInfo(legit::Colors::pumpkin, "PassGICasting")
        .SetRecordFunc([this, passData, res](legit::RenderGraph::RenderPassContext passContext)
      {
        auto shaderProgram = listBlockSizedCastingShader.program.get();
        auto pipeineInfo = this->core->GetPipelineCache()->BindGraphicsPipeline(passContext.GetCommandBuffer(), passContext.GetRenderPass()->GetHandle(), legit::DepthSettings::Disabled(), { legit::BlendSettings::Opaque() }, legit::VertexDeclaration(), vk::PrimitiveTopology::eTriangleFan, shaderProgram);
        {
          const legit::DescriptorSetLayoutKey *shaderDataSetInfo = shaderProgram->GetSetInfo(ShaderDataSetIndex);
          auto shaderData = passData.memoryPool->BeginSet(shaderDataSetInfo);
          {
            auto shaderDataBuffer = passData.memoryPool->GetUniformBufferData<ListBlockSizedCastingShader::DataBuffer>("BucketCastData");

            shaderDataBuffer->viewMatrix = passData.bucketViewMatrix;
            shaderDataBuffer->projMatrix = passData.bucketProjMatrix;
            shaderDataBuffer->totalBucketsCount = glm::uint(res.totalBucketsCount);
            shaderDataBuffer->time = 0.0f;
            shaderDataBuffer->frameIndex = passData.frameIndex;
            shaderDataBuffer->debugMip = -1;
            shaderDataBuffer->debugType = -1;
          }
          passData.memoryPool->EndSet();


          std::vector<legit::StorageBufferBinding> storageBufferBindings;
          auto bucketsBuffer = passContext.GetBuffer(res.bucketsProxyId);
          storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("BucketsBuffer", bucketsBuffer));

          auto mipInfosBuffer = passContext.GetBuffer(res.mipInfosProxyId);
          storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("MipInfosBuffer", mipInfosBuffer));

          auto pointsListBuffer = passContext.GetBuffer(res.pointsListProxyId);
          storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("PointsListBuffer", pointsListBuffer));

          auto blockPointsListBuffer = passContext.GetBuffer(res.blockPointsListProxyId);
          storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("BlockPointsListBuffer", blockPointsListBuffer));

          auto pointsBuffer = passContext.GetBuffer(this->sceneResources->pointData->Id());
          storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("PointsBuffer", pointsBuffer));

          std::vector<legit::ImageSamplerBinding> imageSamplerBindings;
          imageSamplerBindings.push_back(shaderDataSetInfo->MakeImageSamplerBinding("brushSampler", brushImageView.get(), screenspaceSampler.get()));


          auto shaderDataSet = this->core->GetDescriptorSetCache()->GetDescriptorSet(*shaderDataSetInfo, shaderData.uniformBufferBindings, storageBufferBindings, imageSamplerBindings);
          passContext.GetCommandBuffer().bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeineInfo.pipelineLayout, ShaderDataSetIndex, { shaderDataSet }, { shaderData.dynamicOffset });

          passContext.GetCommandBuffer().draw(4, 1, 0, 0);
        }
      }));
    }
    //final gathering
    /*core->GetRenderGraph()->AddPass( legit::RenderGraph::RenderPassDesc()
      .SetColorAttachments({
        {frameInfo.swapchainImageViewProxyId, vk::AttachmentLoadOp::eDontCare } })
      .SetStorageBuffers({
        this->sceneResources->pointData->Id(),
        this->viewportResources->bucketData->Id() })
      .SetInputImages({ viewportResources->indexPyramidPing.mipImageViewProxies[0]->Id() })
      .SetRenderAreaExtent(this->viewportExtent)
      .SetProfilerInfo(legit::Colors::turqoise, "PassGathering")
      .SetRecordFunc([this, passData](legit::RenderGraph::RenderPassContext passContext)
    {
      auto shaderProgram = finalGatheringShader.program.get();
      auto pipeineInfo = this->core->GetPipelineCache()->BindGraphicsPipeline(passContext.GetCommandBuffer(), passContext.GetRenderPass()->GetHandle(), legit::DepthSettings::Disabled(), { legit::BlendSettings::Opaque() }, legit::VertexDeclaration(), vk::PrimitiveTopology::eTriangleFan, shaderProgram);
      {
        const legit::DescriptorSetLayoutKey *shaderDataSetInfo = shaderProgram->GetSetInfo(ShaderDataSetIndex);
        auto shaderData = passData.memoryPool->BeginSet(shaderDataSetInfo);
        {
          auto shaderDataBuffer = passData.memoryPool->GetUniformBufferData<FinalGatheringShader::DataBuffer>("PassData");

          shaderDataBuffer->viewMatrix = passData.viewMatrix;
          shaderDataBuffer->projMatrix = passData.projMatrix;
          shaderDataBuffer->viewportSize = glm::vec4(this->viewportExtent.width, this->viewportExtent.height, 0.0f, 0.0f);
          shaderDataBuffer->bucketViewportSize = glm::vec4(this->viewportResources->tmpBucketTexture.baseSize.x, this->viewportResources->tmpBucketTexture.baseSize.y, 0.0f, 0.0f);
          shaderDataBuffer->pointWorldSize = passData.pointWorldSize;
          shaderDataBuffer->time = 0.0f;
        }
        passData.memoryPool->EndSet();

        std::vector<legit::ImageSamplerBinding> imageSamplerBindings;
        auto pointIndicesImageView = passContext.GetImageView(this->viewportResources->indexPyramidPing.mipImageViewProxies[0]->Id());
        imageSamplerBindings.push_back(shaderDataSetInfo->MakeImageSamplerBinding("pointIndicesSampler", pointIndicesImageView, screenspaceSampler.get()));
        imageSamplerBindings.push_back(shaderDataSetInfo->MakeImageSamplerBinding("brushSampler", brushImageView.get(), screenspaceSampler.get()));

        std::vector<legit::StorageBufferBinding> storageBufferBindings;
        auto pointDataBuffer = passContext.GetBuffer(this->sceneResources->pointData->Id());
        storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("PointsBuffer", pointDataBuffer));
        auto bucketDataBuffer = passContext.GetBuffer(this->viewportResources->bucketData->Id());
        storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("BucketData", bucketDataBuffer));

        auto shaderDataSet = this->core->GetDescriptorSetCache()->GetDescriptorSet(*shaderDataSetInfo, shaderData.uniformBufferBindings, storageBufferBindings, imageSamplerBindings);
        passContext.GetCommandBuffer().bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeineInfo.pipelineLayout, ShaderDataSetIndex, { shaderDataSet }, { shaderData.dynamicOffset });
        passContext.GetCommandBuffer().draw(4, 1, 0, 0);
      }
    }));*/


    ImGui::Checkbox("Use array buckets", &useArrayBuckets);
    ImGui::Checkbox("Use block gathering", &useBlockGathering);
    ImGui::Checkbox("Use sized gathering", &useSizedGathering);
    ImGui::SliderInt("Debug mip", &debugMip, -1, 10);
    ImGui::SliderInt("Debug type", &debugType, -1, 3);


    if(useArrayBuckets)
    {
      auto res = arrayBucketeer.BucketPoints(frameInfo.memoryPool, passData.projMatrix, passData.viewMatrix, this->sceneResources->pointData->Id(), uint32_t(sceneResources->pointsCount), true);

      core->GetRenderGraph()->AddPass( legit::RenderGraph::RenderPassDesc()
        .SetColorAttachments({
          {frameInfo.swapchainImageViewProxyId, vk::AttachmentLoadOp::eDontCare } })
        .SetStorageBuffers({
          this->sceneResources->pointData->Id(),
          res.bucketsProxyId,
          res.mipInfosProxyId,
          res.bucketEntriesPoolProxyId,
          res.bucketGroupsProxyId,
          res.groupEntriesPoolProxyId })
        .SetInputImages({ viewportResources->indexPyramidPing.mipImageViewProxies[0]->Id() })
        .SetRenderAreaExtent(this->viewportExtent)
        .SetProfilerInfo(legit::Colors::turqoise, "PassBcktGathering")
        .SetRecordFunc([this, passData, res](legit::RenderGraph::RenderPassContext passContext)
      {
        auto shaderProgram = arrayBucketGatheringShader.program.get();
        auto pipeineInfo = this->core->GetPipelineCache()->BindGraphicsPipeline(passContext.GetCommandBuffer(), passContext.GetRenderPass()->GetHandle(), legit::DepthSettings::Disabled(), { legit::BlendSettings::Opaque() }, legit::VertexDeclaration(), vk::PrimitiveTopology::eTriangleFan, shaderProgram);
        {
          const legit::DescriptorSetLayoutKey *shaderDataSetInfo = shaderProgram->GetSetInfo(ShaderDataSetIndex);
          auto shaderData = passData.memoryPool->BeginSet(shaderDataSetInfo);
          {
            auto shaderDataBuffer = passData.memoryPool->GetUniformBufferData<ArrayBucketGatheringShader::DataBuffer>("PassData");

            shaderDataBuffer->viewMatrix = passData.viewMatrix;
            shaderDataBuffer->projMatrix = passData.projMatrix;
            shaderDataBuffer->viewportSize = glm::vec4(this->viewportExtent.width, this->viewportExtent.height, 0.0f, 0.0f);
            shaderDataBuffer->time = 0.0f;
          }
          passData.memoryPool->EndSet();


          std::vector<legit::StorageBufferBinding> storageBufferBindings;
          auto bucketsBuffer = passContext.GetBuffer(res.bucketsProxyId);
          storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("BucketsBuffer", bucketsBuffer));

          auto mipInfosBuffer = passContext.GetBuffer(res.mipInfosProxyId);
          storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("MipInfosBuffer", mipInfosBuffer));

          auto bucketEntriesPoolBuffer = passContext.GetBuffer(res.bucketEntriesPoolProxyId);
          storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("BucketEntriesPoolBuffer", bucketEntriesPoolBuffer));

          auto bucketGroupsBuffer = passContext.GetBuffer(res.bucketGroupsProxyId);
          storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("BucketGroupsBuffer", bucketGroupsBuffer));

          auto groupEntriesPoolBuffer = passContext.GetBuffer(res.groupEntriesPoolProxyId);
          storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("GroupEntriesPoolBuffer", groupEntriesPoolBuffer));

          auto pointsDataBuffer = passContext.GetBuffer(this->sceneResources->pointData->Id());
          storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("PointsBuffer", pointsDataBuffer));

          std::vector<legit::ImageSamplerBinding> imageSamplerBindings;
          imageSamplerBindings.push_back(shaderDataSetInfo->MakeImageSamplerBinding("brushSampler", brushImageView.get(), screenspaceSampler.get()));


          auto shaderDataSet = this->core->GetDescriptorSetCache()->GetDescriptorSet(*shaderDataSetInfo, shaderData.uniformBufferBindings, storageBufferBindings, imageSamplerBindings);
          passContext.GetCommandBuffer().bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeineInfo.pipelineLayout, ShaderDataSetIndex, { shaderDataSet }, { shaderData.dynamicOffset });
          passContext.GetCommandBuffer().draw(4, 1, 0, 0);
        }
      }));
    }else
    {
      auto res = listBucketeer.BucketPoints(frameInfo.memoryPool, passData.projMatrix, passData.viewMatrix, this->sceneResources->pointData->Id(), uint32_t(sceneResources->pointsCount), true);

      if(useBlockGathering)
      {
        core->GetRenderGraph()->AddPass( legit::RenderGraph::RenderPassDesc()
          .SetColorAttachments({
            {frameInfo.swapchainImageViewProxyId, vk::AttachmentLoadOp::eDontCare } })
          .SetStorageBuffers({
            this->sceneResources->pointData->Id(),
            res.bucketsProxyId,
            res.mipInfosProxyId,
            res.pointsListProxyId,
            res.blockPointsListProxyId })
          .SetRenderAreaExtent(this->viewportExtent)
          .SetProfilerInfo(legit::Colors::turqoise, "PassBlkGathering")
          .SetRecordFunc([this, passData, res](legit::RenderGraph::RenderPassContext passContext)
        {
          auto shaderProgram = this->useSizedGathering ? listBlockSizedGatheringShader.program.get() : listBlockGatheringShader.program.get();
          auto pipeineInfo = this->core->GetPipelineCache()->BindGraphicsPipeline(passContext.GetCommandBuffer(), passContext.GetRenderPass()->GetHandle(), legit::DepthSettings::Disabled(), { legit::BlendSettings::Opaque() }, legit::VertexDeclaration(), vk::PrimitiveTopology::eTriangleFan, shaderProgram);
          {
            const legit::DescriptorSetLayoutKey *shaderDataSetInfo = shaderProgram->GetSetInfo(ShaderDataSetIndex);
            auto shaderData = passData.memoryPool->BeginSet(shaderDataSetInfo);
            {
              auto shaderDataBuffer = passData.memoryPool->GetUniformBufferData<ListBucketGatheringShader::DataBuffer>("PassDataBuffer");

              shaderDataBuffer->viewMatrix = passData.viewMatrix;
              shaderDataBuffer->projMatrix = passData.projMatrix;
              shaderDataBuffer->viewportSize = glm::vec4(this->viewportExtent.width, this->viewportExtent.height, 0.0f, 0.0f);
              shaderDataBuffer->time = 0.0f;
              shaderDataBuffer->framesCount = passData.frameIndex;
              shaderDataBuffer->debugMip = this->debugMip;
              shaderDataBuffer->debugType = this->debugType;
            }
            passData.memoryPool->EndSet();


            std::vector<legit::StorageBufferBinding> storageBufferBindings;
            auto bucketsBuffer = passContext.GetBuffer(res.bucketsProxyId);
            storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("BucketsBuffer", bucketsBuffer));

            auto mipInfosBuffer = passContext.GetBuffer(res.mipInfosProxyId);
            storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("MipInfosBuffer", mipInfosBuffer));

            auto pointsListBuffer = passContext.GetBuffer(res.pointsListProxyId);
            storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("PointsListBuffer", pointsListBuffer));

            auto blockPointsListBuffer = passContext.GetBuffer(res.blockPointsListProxyId);
            storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("BlockPointsListBuffer", blockPointsListBuffer));

            auto pointsBuffer = passContext.GetBuffer(this->sceneResources->pointData->Id());
            storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("PointsBuffer", pointsBuffer));

            std::vector<legit::ImageSamplerBinding> imageSamplerBindings;
            imageSamplerBindings.push_back(shaderDataSetInfo->MakeImageSamplerBinding("brushSampler", brushImageView.get(), screenspaceSampler.get()));


            auto shaderDataSet = this->core->GetDescriptorSetCache()->GetDescriptorSet(*shaderDataSetInfo, shaderData.uniformBufferBindings, storageBufferBindings, imageSamplerBindings);
            passContext.GetCommandBuffer().bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeineInfo.pipelineLayout, ShaderDataSetIndex, { shaderDataSet }, { shaderData.dynamicOffset });

            passContext.GetCommandBuffer().draw(4, 1, 0, 0);
          }
        }));
      }
      else
      {
        core->GetRenderGraph()->AddPass( legit::RenderGraph::RenderPassDesc()
          .SetColorAttachments({
            {frameInfo.swapchainImageViewProxyId, vk::AttachmentLoadOp::eDontCare } })
          .SetStorageBuffers({
            this->sceneResources->pointData->Id(),
            res.bucketsProxyId,
            res.mipInfosProxyId,
            res.pointsListProxyId})
          .SetRenderAreaExtent(this->viewportExtent)
          .SetProfilerInfo(legit::Colors::turqoise, "PassBcktGathering")
          .SetRecordFunc([this, passData, res](legit::RenderGraph::RenderPassContext passContext)
        {
          auto shaderProgram = listBucketGatheringShader.program.get();
          auto pipeineInfo = this->core->GetPipelineCache()->BindGraphicsPipeline(passContext.GetCommandBuffer(), passContext.GetRenderPass()->GetHandle(), legit::DepthSettings::Disabled(), { legit::BlendSettings::Opaque() }, legit::VertexDeclaration(), vk::PrimitiveTopology::eTriangleFan, shaderProgram);
          {
            const legit::DescriptorSetLayoutKey *shaderDataSetInfo = shaderProgram->GetSetInfo(ShaderDataSetIndex);
            auto shaderData = passData.memoryPool->BeginSet(shaderDataSetInfo);
            {
              auto shaderDataBuffer = passData.memoryPool->GetUniformBufferData<ListBucketGatheringShader::DataBuffer>("PassDataBuffer");

              shaderDataBuffer->viewMatrix = passData.viewMatrix;
              shaderDataBuffer->projMatrix = passData.projMatrix;
              shaderDataBuffer->viewportSize = glm::vec4(this->viewportExtent.width, this->viewportExtent.height, 0.0f, 0.0f);
              shaderDataBuffer->time = 0.0f;
              shaderDataBuffer->framesCount = passData.frameIndex;
            }
            passData.memoryPool->EndSet();


            std::vector<legit::StorageBufferBinding> storageBufferBindings;
            auto bucketsBuffer = passContext.GetBuffer(res.bucketsProxyId);
            storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("BucketsBuffer", bucketsBuffer));

            auto mipInfosBuffer = passContext.GetBuffer(res.mipInfosProxyId);
            storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("MipInfosBuffer", mipInfosBuffer));

            auto pointsListBuffer = passContext.GetBuffer(res.pointsListProxyId);
            storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("PointsListBuffer", pointsListBuffer));

            auto pointsBuffer = passContext.GetBuffer(this->sceneResources->pointData->Id());
            storageBufferBindings.push_back(shaderDataSetInfo->MakeStorageBufferBinding("PointsBuffer", pointsBuffer));

            std::vector<legit::ImageSamplerBinding> imageSamplerBindings;
            imageSamplerBindings.push_back(shaderDataSetInfo->MakeImageSamplerBinding("brushSampler", brushImageView.get(), screenspaceSampler.get()));


            auto shaderDataSet = this->core->GetDescriptorSetCache()->GetDescriptorSet(*shaderDataSetInfo, shaderData.uniformBufferBindings, storageBufferBindings, imageSamplerBindings);
            passContext.GetCommandBuffer().bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeineInfo.pipelineLayout, ShaderDataSetIndex, { shaderDataSet }, { shaderData.dynamicOffset });
            passContext.GetCommandBuffer().draw(4, 1, 0, 0);
          }
        }));
      }
    }

    std::vector<legit::RenderGraph::ImageViewProxyId> debugProxies;
    //debugProxies = viewportResources->blurredDirectLight.mipImageViewProxies;
    debugProxies.push_back(viewportResources->shadowmapDebug.imageViewProxy->Id());
    debugRenderer.RenderImageViews(core->GetRenderGraph(), frameInfo.memoryPool, frameInfo.swapchainImageViewProxyId, debugProxies);
    ImGui::End();
  }

  void ReloadShaders()
  {
    pointRasterizerShader.vertex.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/PointRenderer/pointRasterizer.vert.spv"));
    pointRasterizerShader.fragment.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/PointRenderer/pointRasterizer.frag.spv"));
    pointRasterizerShader.program.reset(new legit::ShaderProgram(pointRasterizerShader.vertex.get(), pointRasterizerShader.fragment.get()));

    pointSpritesShader.vertex.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/PointRenderer/pointSprites.vert.spv"));
    pointSpritesShader.fragment.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/PointRenderer/pointSprites.frag.spv"));
    pointSpritesShader.program.reset(new legit::ShaderProgram(pointSpritesShader.vertex.get(), pointSpritesShader.fragment.get()));

    splattingShader.vertex.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/Common/screenspaceQuad.vert.spv"));
    splattingShader.fragment.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/PointRenderer/splatting.frag.spv"));
    splattingShader.program.reset(new legit::ShaderProgram(splattingShader.vertex.get(), splattingShader.fragment.get()));

    mipBuilderShader.vertex.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/Common/screenspaceQuad.vert.spv"));
    mipBuilderShader.fragment.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/PointRenderer/mipBuilder.frag.spv"));
    mipBuilderShader.program.reset(new legit::ShaderProgram(mipBuilderShader.vertex.get(), mipBuilderShader.fragment.get()));

    directLightShader.vertex.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/Common/screenspaceQuad.vert.spv"));
    directLightShader.fragment.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/PointRenderer/directLightSplatting.frag.spv"));
    directLightShader.program.reset(new legit::ShaderProgram(directLightShader.vertex.get(), directLightShader.fragment.get()));

    jumpFillShader.vertex.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/Common/screenspaceQuad.vert.spv"));
    jumpFillShader.fragment.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/PointRenderer/jumpFill.frag.spv"));
    jumpFillShader.program.reset(new legit::ShaderProgram(jumpFillShader.vertex.get(), jumpFillShader.fragment.get()));

    listBucketCastShader.compute.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/PointRenderer/listBucketCast.comp.spv"));

    finalGatheringShader.vertex.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/Common/screenspaceQuad.vert.spv"));
    finalGatheringShader.fragment.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/PointRenderer/finalGathering.frag.spv"));
    finalGatheringShader.program.reset(new legit::ShaderProgram(finalGatheringShader.vertex.get(), finalGatheringShader.fragment.get()));

    arrayBucketGatheringShader.vertex.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/Common/screenspaceQuad.vert.spv"));
    arrayBucketGatheringShader.fragment.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/PointRenderer/arrayBucketGathering.frag.spv"));
    arrayBucketGatheringShader.program.reset(new legit::ShaderProgram(arrayBucketGatheringShader.vertex.get(), arrayBucketGatheringShader.fragment.get()));

    listBucketGatheringShader.vertex.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/Common/screenspaceQuad.vert.spv"));
    listBucketGatheringShader.fragment.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/PointRenderer/listBucketGathering.frag.spv"));
    listBucketGatheringShader.program.reset(new legit::ShaderProgram(listBucketGatheringShader.vertex.get(), listBucketGatheringShader.fragment.get()));

    listBlockGatheringShader.vertex.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/Common/screenspaceQuad.vert.spv"));
    listBlockGatheringShader.fragment.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/PointRenderer/listBlockGathering.frag.spv"));
    listBlockGatheringShader.program.reset(new legit::ShaderProgram(listBlockGatheringShader.vertex.get(), listBlockGatheringShader.fragment.get()));

    listBlockSizedGatheringShader.vertex.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/Common/screenspaceQuad.vert.spv"));
    listBlockSizedGatheringShader.fragment.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/PointRenderer/listBlockSizedGathering.frag.spv"));
    listBlockSizedGatheringShader.program.reset(new legit::ShaderProgram(listBlockSizedGatheringShader.vertex.get(), listBlockSizedGatheringShader.fragment.get()));

    listBlockSizedCastingShader.vertex.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/Common/screenspaceQuad.vert.spv"));
    listBlockSizedCastingShader.fragment.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/PointRenderer/listBlockSizedCasting.frag.spv"));
    listBlockSizedCastingShader.program.reset(new legit::ShaderProgram(listBlockSizedCastingShader.vertex.get(), listBlockSizedCastingShader.fragment.get()));

    listDirectLightShader.compute.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/PointRenderer/listDirectLight.comp.spv"));

    mipBuilder.ReloadShaders();
    blurBuilder.ReloadShaders();
    debugRenderer.ReloadShaders();
    arrayBucketeer.ReloadShaders();
    listBucketeer.ReloadShaders();
  }
private:


  const static uint32_t ShaderDataSetIndex = 0;
  const static uint32_t DrawCallDataSetIndex = 1;

  legit::VertexDeclaration vertexDecl;

  #pragma pack(push, 1)
  struct Point
  {
    glm::vec4 worldPos;
    glm::vec4 worldNorm;
    glm::vec4 directLight;
    glm::vec4 indirectLight;
    glm::uint next;
    glm::vec3 padding;
  };
  #pragma pack(pop)

  #pragma pack(push, 1)
  struct Bucket
  {
    glm::uint pointIndex;
    glm::uint pointsCount;
  };
  #pragma pack(pop)
  
  struct SceneResources
  {
    SceneResources(legit::Core *core, size_t pointsCount)
    {
      this->pointsCount = pointsCount;
      pointBuffer = std::unique_ptr<legit::Buffer>(new legit::Buffer(core->GetPhysicalDevice(), core->GetLogicalDevice(), sizeof(Point) * pointsCount, vk::BufferUsageFlagBits::eStorageBuffer, vk::MemoryPropertyFlagBits::eDeviceLocal));
      pointData = core->GetRenderGraph()->AddExternalBuffer(pointBuffer.get());
    }
    std::unique_ptr<legit::Buffer> pointBuffer;
    legit::RenderGraph::BufferProxyUnique pointData;
    size_t pointsCount;
  };
  std::unique_ptr<SceneResources> sceneResources;

  struct ViewportResources
  {
    ViewportResources(legit::RenderGraph *renderGraph, glm::uvec2 screenSize) :
      depthStencil(renderGraph, vk::Format::eD32Sfloat, screenSize, legit::depthImageUsage),
      indexPyramidPing(renderGraph, vk::Format::eR32Uint, screenSize, legit::colorImageUsage),
      indexPyramidPong(renderGraph, vk::Format::eR32Uint, screenSize, legit::colorImageUsage),
      shadowmapIndexPyramid(renderGraph, vk::Format::eR32Uint, glm::uvec2(1024, 1024), legit::colorImageUsage),
      shadowmapDepthStencil(renderGraph, vk::Format::eD32Sfloat, glm::uvec2(1024, 1024), legit::depthImageUsage),
      shadowmapDebug(renderGraph, vk::Format::eR8G8B8A8Unorm, glm::uvec2(1024, 1024), legit::colorImageUsage)
    {
    }

    UnmippedProxy depthStencil;
    MippedProxy indexPyramidPing;
    MippedProxy indexPyramidPong;
    UnmippedProxy shadowmapDepthStencil;
    UnmippedProxy shadowmapDebug;
    MippedProxy shadowmapIndexPyramid;
  };
  std::unique_ptr<ViewportResources> viewportResources;
  vk::Extent2D viewportExtent;

  struct PointRasterizerShader
  {
    #pragma pack(push, 1)
    struct DataBuffer
    {
      glm::mat4 viewMatrix;
      glm::mat4 projMatrix;
      glm::vec4 viewportSize;
      float time;
    };
    #pragma pack(pop)

    std::unique_ptr<legit::Shader> vertex;
    std::unique_ptr<legit::Shader> fragment;
    std::unique_ptr<legit::ShaderProgram> program;
  } pointRasterizerShader;

  struct PointSpritesShader
  {
    #pragma pack(push, 1)
    struct DataBuffer
    {
      glm::mat4 viewMatrix; //world -> camera
      glm::mat4 projMatrix; //camera -> ndc
      glm::vec4 viewportSize;
      float pointWorldSize;
      float fovy;
      float time;
    };
    #pragma pack(pop)

    std::unique_ptr<legit::Shader> vertex;
    std::unique_ptr<legit::Shader> fragment;
    std::unique_ptr<legit::ShaderProgram> program;
  } pointSpritesShader;

  struct MipBuilderShader
  {
    #pragma pack(push, 1)
    struct DataBuffer
    {
      glm::mat4 viewMatrix;
      glm::mat4 projMatrix;
      glm::vec4 currViewportSize;
      glm::vec4 prevViewportSize;
      float time;
    };
    #pragma pack(pop)

    std::unique_ptr<legit::Shader> vertex;
    std::unique_ptr<legit::Shader> fragment;
    std::unique_ptr<legit::ShaderProgram> program;
  } mipBuilderShader;

  struct SplattingShader
  {
    #pragma pack(push, 1)
    struct DataBuffer
    {
      glm::mat4 viewMatrix;
      glm::mat4 projMatrix;
      glm::vec4 viewportSize;
      float time;
    };
    #pragma pack(pop)

    std::unique_ptr<legit::Shader> vertex;
    std::unique_ptr<legit::Shader> fragment;
    std::unique_ptr<legit::ShaderProgram> program;
  } splattingShader;

  struct DirectLightShader
  {
    #pragma pack(push, 1)
    struct DataBuffer
    {
      glm::mat4 lightViewMatrix;
      glm::mat4 lightProjMatrix;
      glm::vec4 lightViewportSize;
      float time;
    };
    #pragma pack(pop)

    std::unique_ptr<legit::Shader> vertex;
    std::unique_ptr<legit::Shader> fragment;
    std::unique_ptr<legit::ShaderProgram> program;
  } directLightShader;

  struct JumpFillShader
  {
    #pragma pack(push, 1)
    struct DataBuffer
    {
      glm::mat4 viewMatrix;
      glm::mat4 projMatrix;
      glm::vec4 viewportSize;
      float time;
      int jumpDist;
    };
    #pragma pack(pop)

    std::unique_ptr<legit::Shader> vertex;
    std::unique_ptr<legit::Shader> fragment;
    std::unique_ptr<legit::ShaderProgram> program;
  } jumpFillShader;
  
  struct ListBucketCastShader
  {
    #pragma pack(push, 1)
    struct DataBuffer
    {
      glm::mat4 viewMatrix;
      glm::mat4 projMatrix;
      glm::uint totalBucketsCount;
      float time;
      int32_t frameIndex;
    };
    #pragma pack(pop)

    std::unique_ptr<legit::Shader> compute;
  } listBucketCastShader;

  struct ListDirectLightShader
  {
    #pragma pack(push, 1)
    struct DataBuffer
    {
      glm::mat4 lightViewMatrix;
      glm::mat4 lightProjMatrix;
      glm::vec4 lightViewportSize;
      float time;
      glm::uint totalBucketsCount;
    };
    #pragma pack(pop)

    std::unique_ptr<legit::Shader> compute;
  } listDirectLightShader;

  struct FinalGatheringShader
  {
    #pragma pack(push, 1)
    struct DataBuffer
    {
      glm::mat4 viewMatrix;
      glm::mat4 projMatrix;
      glm::vec4 viewportSize;
      glm::vec4 bucketViewportSize;
      float pointWorldSize;
      float time;
    };
    #pragma pack(pop)

    std::unique_ptr<legit::Shader> vertex;
    std::unique_ptr<legit::Shader> fragment;
    std::unique_ptr<legit::ShaderProgram> program;
  } finalGatheringShader;

  struct ArrayBucketGatheringShader
  {
    #pragma pack(push, 1)
    struct DataBuffer
    {
      glm::mat4 viewMatrix;
      glm::mat4 projMatrix;
      glm::vec4 viewportSize;
      glm::uint bucketGroupsCount;
      float time;
    };
    #pragma pack(pop)

    std::unique_ptr<legit::Shader> vertex;
    std::unique_ptr<legit::Shader> fragment;
    std::unique_ptr<legit::ShaderProgram> program;
  } arrayBucketGatheringShader;

  struct ListBucketGatheringShader
  {
    #pragma pack(push, 1)
    struct DataBuffer
    {
      glm::mat4 viewMatrix;
      glm::mat4 projMatrix;
      glm::vec4 viewportSize;
      glm::uint bucketGroupsCount;
      float time;
      glm::uint framesCount;
      int debugMip;
      int debugType;
    };
    #pragma pack(pop)

    std::unique_ptr<legit::Shader> vertex;
    std::unique_ptr<legit::Shader> fragment;
    std::unique_ptr<legit::ShaderProgram> program;
  } listBucketGatheringShader;

  struct ListBlockGatheringShader
  {
    #pragma pack(push, 1)
    struct DataBuffer
    {
      glm::mat4 viewMatrix;
      glm::mat4 projMatrix;
      glm::vec4 viewportSize;
      glm::uint bucketGroupsCount;
      float time;
    };
    #pragma pack(pop)

    std::unique_ptr<legit::Shader> vertex;
    std::unique_ptr<legit::Shader> fragment;
    std::unique_ptr<legit::ShaderProgram> program;
  } listBlockGatheringShader;

  struct ListBlockSizedGatheringShader
  {
    std::unique_ptr<legit::Shader> vertex;
    std::unique_ptr<legit::Shader> fragment;
    std::unique_ptr<legit::ShaderProgram> program;
  } listBlockSizedGatheringShader;

  struct ListBlockSizedCastingShader
  {
    #pragma pack(push, 1)
    struct DataBuffer
    {
      glm::mat4 viewMatrix;
      glm::mat4 projMatrix;
      glm::uint totalBucketsCount;
      float time;
      int32_t frameIndex;
      int debugMip;
      int debugType;
    };
    #pragma pack(pop)

    std::unique_ptr<legit::Shader> vertex;
    std::unique_ptr<legit::Shader> fragment;
    std::unique_ptr<legit::ShaderProgram> program;
  } listBlockSizedCastingShader;

  //vk::Extent2D viewportSize;
  MipBuilder mipBuilder;
  BlurBuilder blurBuilder;
  DebugRenderer debugRenderer;
  ArrayBucketeer arrayBucketeer;
  ListBucketeer listBucketeer;
  ListBucketeer giBucketeer;
  ListBucketeer directLightBucketeer;

  bool useArrayBuckets;
  bool useBlockGathering;
  bool useSizedGathering;
  int debugMip;
  int debugType;

  std::unique_ptr<legit::Sampler> screenspaceSampler;
  std::unique_ptr<legit::Sampler> shadowmapSampler;

  std::unique_ptr<legit::Image> brushImage;
  std::unique_ptr<legit::ImageView> brushImageView;

  glm::uvec2 giViewportSize;

  std::default_random_engine eng;
  std::uniform_real_distribution<float> dis{ 0.0f, 1.0f };

  legit::Core *core;
};