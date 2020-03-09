#include "../../Common/MipBuilder.h"
#include "../../Common/BlurBuilder.h"
#include "../../Common/ArrayBucketeer.h"
#include "../../Common/ListBucketeer.h"
#include "../../Common/DebugRenderer.h"
#include "ShrodingerSolver.h"
//#include "SimpleSolver.h"

class WaterParticleRenderer : public BaseRenderer
{
public:
  WaterParticleRenderer(legit::Core *_core) :
    mipBuilder(_core),
    blurBuilder(_core),
    debugRenderer(_core),
    viewportBucketeer(_core),
    giBucketeer(_core),
    directLightBucketeer(_core),
    solver(_core)
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

    debugMip = -1;
    debugType = -1;
  }

  void RecreateSceneResources(Scene *scene)
  {
    sceneResources.reset(new SceneResources(core, glm::vec3(-2.5f, -2.5f, -2.5f), glm::vec3(2.5f, 2.5f, 2.5f)));
    viewportBucketeer.RecreateSceneResources(sceneResources->pointsCount);
    giBucketeer.RecreateSceneResources(sceneResources->pointsCount);
    directLightBucketeer.RecreateSceneResources(sceneResources->pointsCount);

    solver.RecreateSceneResources(glm::uvec3(128, 128, 128), sceneResources->volumeMin, sceneResources->volumeMax);
  }
  
  void RecreateSwapchainResources(vk::Extent2D viewportExtent, size_t framesInFlightCount)
  {
    this->viewportExtent = viewportExtent;

    glm::uvec2 viewportSize = { viewportExtent.width, viewportExtent.height };
    viewportResources.reset(new ViewportResources(core->GetRenderGraph(), viewportSize));

    viewportBucketeer.RecreateSwapchainResources(glm::uvec2(viewportExtent.width / 4, viewportExtent.height / 4), framesInFlightCount);
    this->giViewportSize = glm::uvec2(128, 128);
    giBucketeer.RecreateSwapchainResources(giViewportSize, framesInFlightCount, 1);
    directLightBucketeer.RecreateSwapchainResources(glm::uvec2(256, 256), framesInFlightCount);
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
    float lightAbsorb;
    float splatOpacity;
    Scene *scene;
  };


  public:
  void RenderFrame(const legit::InFlightQueue::FrameInfo &frameInfo, const Camera &camera, const Camera &light, Scene *scene, GLFWwindow *window)
  {
    ImGui::Begin("Point renderer stuff");

    static float ang = 0.0f;

    static bool updateSimulation = true;
    ImGui::Checkbox("Update", &updateSimulation);
    if (updateSimulation)
    {
      solver.Update(frameInfo.memoryPool);
      solver.AdvectParticles(frameInfo.memoryPool, sceneResources->pointData->Id(), sceneResources->pointsCount);
    }

    PassData passData;

    passData.memoryPool = frameInfo.memoryPool;
    passData.shadowmapViewportSize = vk::Extent2D(viewportResources->shadowmapIndexPyramid.baseSize.x, viewportResources->shadowmapIndexPyramid.baseSize.y);
    passData.scene = scene;
    passData.viewMatrix = glm::inverse(camera.GetTransformMatrix());
    passData.lightViewMatrix = glm::inverse(light.GetTransformMatrix());
    passData.lightAbsorb = 20.0f;
    passData.splatOpacity = 0.7f;

    passData.fovy = 1.0f;
    passData.pointWorldSize = 0.1f;
    //passData.swapchainImageViewProxyId = frameInfo.swapchainImageViewProxyId;
    float aspect = float(this->viewportExtent.width) / float(this->viewportExtent.height);
    passData.projMatrix = glm::perspectiveZO(passData.fovy, aspect, 0.01f, 1000.0f) * glm::scale(glm::vec3(1.0f, -1.0f, -1.0f));
    passData.lightProjMatrix = glm::perspectiveZO(0.8f, 1.0f, 0.1f, 100.0f) * glm::scale(glm::vec3(1.0f, -1.0f, -1.0f));
    float bucketSize = 5.0f;
    passData.bucketProjMatrix = glm::orthoZO(-bucketSize, bucketSize, -bucketSize, bucketSize, -bucketSize, bucketSize) * glm::scale(glm::vec3(1.0f, -1.0f, -1.0f));
    static uint32_t frameIndex = 0;
    passData.frameIndex = frameIndex;
    frameIndex++;

    {
      Camera bucketPos;
      bucketPos.horAngle = 2.0f * 3.1415f * dis(eng);
      bucketPos.vertAngle = asin(std::max(-1.0f, std::min(1.0f, 2.0f * dis(eng) - 1.0f)));
      ang += 0.01f;
      passData.bucketViewMatrix = glm::inverse(bucketPos.GetTransformMatrix());

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
            shaderDataBuffer->time = 0.0f;
            shaderDataBuffer->lightAbsorb = passData.lightAbsorb;
            shaderDataBuffer->splatOpacity = passData.splatOpacity;
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

    if(1)
    {
      auto res = giBucketeer.BucketPoints(frameInfo.memoryPool, passData.bucketProjMatrix, passData.bucketViewMatrix, this->sceneResources->pointData->Id(), uint32_t(sceneResources->pointsCount), true);

      //bucket casting
      core->GetRenderGraph()->AddPass(legit::RenderGraph::ComputePassDesc()
        .SetStorageBuffers({
          this->sceneResources->pointData->Id(),
          res.bucketsProxyId,
          res.mipInfosProxyId,
          res.pointsListProxyId })
        .SetProfilerInfo(legit::Colors::pumpkin, "PassBucketCasting")
        .SetRecordFunc([this, res, passData](legit::RenderGraph::PassContext passContext)
      {
        std::vector<legit::BlendSettings> attachmentBlendSettings;
        auto shader = bucketCastingShader.compute.get();
        auto pipeineInfo = this->core->GetPipelineCache()->BindComputePipeline(passContext.GetCommandBuffer(), shader);
        {
          const legit::DescriptorSetLayoutKey *shaderDataSetInfo = shader->GetSetInfo(ShaderDataSetIndex);
          auto shaderData = passData.memoryPool->BeginSet(shaderDataSetInfo);
          {
            auto shaderDataBuffer = passData.memoryPool->GetUniformBufferData<BucketCastingShader::DataBuffer>("BucketCastData");

            shaderDataBuffer->projMatrix = passData.bucketProjMatrix;
            shaderDataBuffer->viewMatrix = passData.bucketViewMatrix;
            shaderDataBuffer->totalBucketsCount = glm::uint(res.totalBucketsCount);
            shaderDataBuffer->time = 0.0f;
            shaderDataBuffer->lightAbsorb = passData.lightAbsorb;
            shaderDataBuffer->splatOpacity = passData.splatOpacity;
            shaderDataBuffer->framesCount= passData.frameIndex;
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
      }));
    }


    ImGui::Checkbox("Use array buckets", &useArrayBuckets);
    ImGui::Checkbox("Use block gathering", &useBlockGathering);
    ImGui::Checkbox("Use sized gathering", &useSizedGathering);
    ImGui::SliderInt("Debug mip", &debugMip, -1, 10);
    ImGui::SliderInt("Debug type", &debugType, -1, 3);



    {
      auto res = viewportBucketeer.BucketPoints(frameInfo.memoryPool, passData.projMatrix, passData.viewMatrix, this->sceneResources->pointData->Id(), uint32_t(sceneResources->pointsCount), true);

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
        auto shaderProgram = listBlockGatheringShader.program.get();
        auto pipeineInfo = this->core->GetPipelineCache()->BindGraphicsPipeline(passContext.GetCommandBuffer(), passContext.GetRenderPass()->GetHandle(), legit::DepthSettings::Disabled(), { legit::BlendSettings::Opaque() }, legit::VertexDeclaration(), vk::PrimitiveTopology::eTriangleFan, shaderProgram);
        {
          const legit::DescriptorSetLayoutKey *shaderDataSetInfo = shaderProgram->GetSetInfo(ShaderDataSetIndex);
          auto shaderData = passData.memoryPool->BeginSet(shaderDataSetInfo);
          {
            auto shaderDataBuffer = passData.memoryPool->GetUniformBufferData<ListBlockGatheringShader::DataBuffer>("PassDataBuffer");

            shaderDataBuffer->viewMatrix = passData.viewMatrix;
            shaderDataBuffer->projMatrix = passData.projMatrix;
            shaderDataBuffer->viewportSize = glm::vec4(this->viewportExtent.width, this->viewportExtent.height, 0.0f, 0.0f);
            shaderDataBuffer->time = 0.0f;
            shaderDataBuffer->framesCount = passData.frameIndex;
            shaderDataBuffer->debugMip = -1;// this->debugMip;
            shaderDataBuffer->debugType = -1;// this->debugType;
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

    directLightShader.vertex.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/Common/screenspaceQuad.vert.spv"));
    directLightShader.fragment.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/PointRenderer/directLightSplatting.frag.spv"));
    directLightShader.program.reset(new legit::ShaderProgram(directLightShader.vertex.get(), directLightShader.fragment.get()));


    listBlockGatheringShader.vertex.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/Common/screenspaceQuad.vert.spv"));
    listBlockGatheringShader.fragment.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/WaterRenderer/finalGathering.frag.spv"));
    listBlockGatheringShader.program.reset(new legit::ShaderProgram(listBlockGatheringShader.vertex.get(), listBlockGatheringShader.fragment.get()));

    bucketCastingShader.compute.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/WaterRenderer/bucketCasting.comp.spv"));
    listDirectLightShader.compute.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/WaterRenderer/directLight.comp.spv"));

    mipBuilder.ReloadShaders();
    blurBuilder.ReloadShaders();
    debugRenderer.ReloadShaders();
    viewportBucketeer.ReloadShaders();
    solver.ReloadShaders();
  }
private:


  const static uint32_t ShaderDataSetIndex = 0;
  const static uint32_t DrawCallDataSetIndex = 1;

  legit::VertexDeclaration vertexDecl;


  #pragma pack(push, 1)
  struct Bucket
  {
    glm::uint pointIndex;
    glm::uint pointsCount;
  };
  #pragma pack(pop)
  
  struct SceneResources
  {
    SceneResources(legit::Core *core, glm::vec3 volumeMin, glm::vec3 volumeMax)
    {
      this->volumeMin = volumeMin;
      this->volumeMax = volumeMax;

      std::vector<Point> points;
      glm::ivec3 size = { 128, 128, 128 };
      glm::vec3 normStep = glm::vec3(1.0f) / glm::vec3(size);

      std::default_random_engine eng;
      std::uniform_real_distribution<float> dis{ 0.0f, 1.0f };

      for (int z = 0; z < size.z; z++)
      {
        for (int y = 0; y < size.y; y++)
        {
          for (int x = 0; x < size.x; x++)
          {
            glm::vec3 normPos = glm::vec3(x, y, z) * normStep;

            Point point;
            glm::vec3 randVec = glm::vec3(dis(eng), dis(eng), dis(eng));
            point.worldPos = glm::vec4((glm::vec3(-0.5f) + normPos + (randVec - glm::vec3(0.5f)) * normStep) * (volumeMax - volumeMin), 1.0f);
            point.worldNormal = glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);

            int step = 10;
            glm::ivec3 code = {
              ((x % (step * 2)) < step ? 0 : 1),
              ((y % (step * 2)) < step ? 0 : 1),
              ((z % (step * 2)) < step ? 0 : 1)};
            //int color = ((code.x + code.y + code.z) % 2);
            int color = (code.x * code.y * code.z);
            float light = 0.0f;

            glm::vec3 leftDiff = normPos - glm::vec3(0.3f, 0.5f, 0.5f);
            glm::vec3 rightDiff = normPos - glm::vec3(0.7f, 0.5f, 0.5f);

            color = 0;
            if (length(leftDiff) < 0.1f)
              color = 1;
            if (length(rightDiff) < 0.1f)
              color = 1;

            if (color == 0) continue;

            point.directLight = glm::vec4(light);
            point.indirectLight = glm::vec4(0.0f);
            point.worldRadius = 0.05f;
            points.push_back(point);
          }
        }
      }
      this->pointsCount = points.size();
      pointBuffer = std::unique_ptr<legit::Buffer>(new legit::Buffer(core->GetPhysicalDevice(), core->GetLogicalDevice(), sizeof(Point) * pointsCount, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eDeviceLocal));
      LoadBufferData(core, points.data(), points.size() * sizeof(Point), pointBuffer.get());

      pointData = core->GetRenderGraph()->AddExternalBuffer(pointBuffer.get());
    }
    glm::vec3 volumeMin;
    glm::vec3 volumeMax;
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

  struct ListDirectLightShader
  {
    #pragma pack(push, 1)
    struct DataBuffer
    {
      glm::mat4 lightViewMatrix;
      glm::mat4 lightProjMatrix;
      float time;
      float lightAbsorb;
      float splatOpacity;
      glm::uint totalBucketsCount;
    };
    #pragma pack(pop)

    std::unique_ptr<legit::Shader> compute;
  } listDirectLightShader;


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
      glm::uint framesCount;
      int debugMip;
      int debugType;
    };
    #pragma pack(pop)

    std::unique_ptr<legit::Shader> vertex;
    std::unique_ptr<legit::Shader> fragment;
    std::unique_ptr<legit::ShaderProgram> program;
  } listBlockGatheringShader;

  struct BucketCastingShader
  {
    #pragma pack(push, 1)
    struct DataBuffer
    {
      glm::mat4 viewMatrix; //world -> camera
      glm::mat4 projMatrix; //camera -> ndc
      int totalBucketsCount;
      float time;
      float lightAbsorb;
      float splatOpacity;
      int framesCount;
    };
    #pragma pack(pop)

    std::unique_ptr<legit::Shader> compute;
  } bucketCastingShader;

  //vk::Extent2D viewportSize;
  MipBuilder mipBuilder;
  BlurBuilder blurBuilder;
  DebugRenderer debugRenderer;

  ListBucketeer viewportBucketeer;
  ListBucketeer giBucketeer;
  ListBucketeer directLightBucketeer;
  ShrodingerSolver solver;
  //SimpleSolver solver;

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