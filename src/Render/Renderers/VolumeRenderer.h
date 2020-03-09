#include "../Common/MipBuilder.h"
#include "../Common/BlurBuilder.h"
#include "../Common/DebugRenderer.h"
#include "../Common/InterleaveBuilder.h"
#include "VolumeRendering/Preintegrator.h"
#include "VolumeRendering/VolumeObject.h"

class VolumeRenderer : public BaseRenderer
{
public:
  VolumeRenderer(legit::Core *_core)
  {
    this->core = _core;

    vertexDecl = Mesh::GetVertexDeclaration();

    screenspaceSampler.reset(new legit::Sampler(core->GetLogicalDevice(), vk::SamplerAddressMode::eClampToEdge, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear));
    cubemapSampler.reset(new legit::Sampler(core->GetLogicalDevice(), vk::SamplerAddressMode::eClampToEdge, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear));
    volumeSampler.reset(new legit::Sampler(core->GetLogicalDevice(), vk::SamplerAddressMode::eClampToEdge, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear));
    preintSampler.reset(new legit::Sampler(core->GetLogicalDevice(), vk::SamplerAddressMode::eClampToEdge, vk::Filter::eNearest, vk::SamplerMipmapMode::eNearest));

    {
      //auto texelData = legit::LoadKtxFromFile("../data/Textures/Cubemaps/1_16f.ktx");
      auto texelData = legit::LoadKtxFromFile("../data/Textures/Cubemaps/3_16f.ktx");
      //auto texelData = legit::LoadKtxFromFile("../data/Textures/Cubemaps/5_16f.ktx");
      auto cubemapCreateDesc = legit::Image::CreateInfoCube(texelData.baseSize, uint32_t(texelData.mips.size()), texelData.format, vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst);
      this->specularCubemap = std::unique_ptr<legit::Image>(new legit::Image(core->GetPhysicalDevice(), core->GetLogicalDevice(), cubemapCreateDesc));
      legit::LoadTexelData(core, &texelData, specularCubemap->GetImageData());
      specularCubemapView = std::unique_ptr<legit::ImageView>(new legit::ImageView(core->GetLogicalDevice(), specularCubemap->GetImageData(), 0, specularCubemap->GetImageData()->GetMipsCount()));
    }

    //std::string volumeMetadata = "../data/Volumes/1/data.json";
    //std::string volumeMetadata = "../data/Volumes/2/data.json";
    std::string volumeMetadata = "../data/Volumes/3/data.json";
    {
      volumeObject = std::unique_ptr<VolumeObject>(new VolumeObject(volumeMetadata));
      //auto resObject = *volumeObject;
      auto resObject = volumeObject->CalculateNormals();

      auto volumeCreateDesc = legit::Image::CreateInfoVolume(resObject.volumeData.baseSize, 1, 1, resObject.volumeData.format, vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst);
      this->volumeImage = std::unique_ptr<legit::Image>(new legit::Image(core->GetPhysicalDevice(), core->GetLogicalDevice(), volumeCreateDesc));
      legit::LoadTexelData(core, &resObject.volumeData, volumeImage->GetImageData());
      this->volumeImageView = std::unique_ptr<legit::ImageView>(new legit::ImageView(core->GetLogicalDevice(), volumeImage->GetImageData(), 0, 1, 0, 1));
    }

    {
      Preintegrator preint;

      auto preintReflectCreateDesc = legit::Image::CreateInfoVolume(preint.preintReflectTex.baseSize, 1, 1, preint.preintReflectTex.format, vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst);
      this->preintReflectImage = std::unique_ptr<legit::Image>(new legit::Image(core->GetPhysicalDevice(), core->GetLogicalDevice(), preintReflectCreateDesc));
      legit::LoadTexelData(core, &preint.preintReflectTex, preintReflectImage->GetImageData());
      this->preintReflectImageView = std::unique_ptr<legit::ImageView>(new legit::ImageView(core->GetLogicalDevice(), preintReflectImage->GetImageData(), 0, 1, 0, 1));

      auto preintScatterCreateDesc = legit::Image::CreateInfoVolume(preint.preintScatterTex.baseSize, 1, 1, preint.preintScatterTex.format, vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst);
      this->preintScatterImage = std::unique_ptr<legit::Image>(new legit::Image(core->GetPhysicalDevice(), core->GetLogicalDevice(), preintReflectCreateDesc));
      legit::LoadTexelData(core, &preint.preintScatterTex, preintScatterImage->GetImageData());
      this->preintScatterImageView = std::unique_ptr<legit::ImageView>(new legit::ImageView(core->GetLogicalDevice(), preintScatterImage->GetImageData(), 0, 1, 0, 1));
    }
    ReloadShaders();
    viewChanged = false;
  }

  void RecreateSceneResources(Scene *scene)
  {

  }
  void RecreateSwapchainResources(vk::Extent2D viewportExtent, size_t inFlightFramesCount)
  {
    auto createInfo = legit::Image::CreateInfo2d(glm::uvec2(viewportExtent.width, viewportExtent.height), 1, 1, vk::Format::eR32G32B32A32Sfloat, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled);
    accumulatedLight.reset(new legit::Image(core->GetPhysicalDevice(), core->GetLogicalDevice(), createInfo));

    legit::ExecuteOnceQueue transferQueue(core);
    auto transferCommandBuffer = transferQueue.BeginCommandBuffer();
    {
      AddTransitionBarrier(accumulatedLight->GetImageData(), legit::ImageUsageTypes::Unknown, legit::ImageUsageTypes::GraphicsShaderRead, transferCommandBuffer);
    }
    transferQueue.EndCommandBuffer();

    accumulatedLightView.reset(new legit::ImageView(core->GetLogicalDevice(), accumulatedLight->GetImageData(), 0, 1, 0, 1));

    glm::uvec2 viewportSize = { viewportExtent.width, viewportExtent.height };
    viewportResources.reset(new ViewportResources(core->GetRenderGraph(), viewportExtent, accumulatedLightView.get()));
  }
  void ChangeView() override
  {
    viewChanged = true;
  }
  void RenderFrame(const legit::InFlightQueue::FrameInfo &frameInfo, const Camera &camera, const Camera &light, Scene *scene, GLFWwindow *window)
  {
    static float time = 0.0f;
    time += 0.01f;
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
      float resetBuffer;
      Scene *scene;
    }passData;

    passData.memoryPool = frameInfo.memoryPool;
    passData.scene = scene;
    passData.viewMatrix = glm::inverse(camera.GetTransformMatrix());
    passData.lightViewMatrix = glm::inverse(light.GetTransformMatrix());
    //passData.swapchainImageViewProxyId = frameInfo.swapchainImageViewProxyId;
    float aspect = float(viewportResources->extent.width) / float(viewportResources->extent.height);

    passData.projMatrix = glm::perspective(1.0f, aspect, 0.01f, 1000.0f) * glm::scale(glm::vec3(1.0f, -1.0f, -1.0f));
    passData.lightProjMatrix = glm::perspective(0.8f, 1.0f, 0.1f, 100.0f) * glm::scale(glm::vec3(1.0f, -1.0f, -1.0f));

    passData.resetBuffer = 0.0f;
    if (viewChanged)
    {
      viewChanged = false;
      viewportResources->totalWeight = 0.0f;
      passData.resetBuffer = 1.0f;
    }

    core->GetRenderGraph()->AddPass(legit::RenderGraph::RenderPassDesc()
      .SetColorAttachments({ viewportResources->accumulatedLightViewProxyId->Id() }, vk::AttachmentLoadOp::eLoad)
      .SetRenderAreaExtent(viewportResources->extent)
      .SetRecordFunc( [this, passData](legit::RenderGraph::RenderPassContext passContext)
    {
      auto pipeineInfo = this->core->GetPipelineCache()->BindGraphicsPipeline(passContext.GetCommandBuffer(), passContext.GetRenderPass()->GetHandle(), legit::DepthSettings::Disabled(), { legit::BlendSettings::Mixed() }, legit::VertexDeclaration(), vk::PrimitiveTopology::eTriangleFan, volumeRendererShader.program.get());
      {
        const legit::DescriptorSetLayoutKey *shaderDataSetInfo = volumeRendererShader.fragment->GetSetInfo(ShaderDataSetIndex);
        auto shaderData = passData.memoryPool->BeginSet(shaderDataSetInfo);
        {
          auto shaderDataBuffer = passData.memoryPool->GetUniformBufferData<VolumeRendererShader::DataBuffer>("VolumeRendererData");

          shaderDataBuffer->viewMatrix = passData.viewMatrix;
          shaderDataBuffer->projMatrix = passData.projMatrix;
          shaderDataBuffer->viewportExtent = glm::vec4(viewportResources->extent.width, viewportResources->extent.height, 0.0f, 0.0f);
          shaderDataBuffer->volumeAABBMin = glm::vec4(this->volumeObject->minPoint, 0.0f);
          shaderDataBuffer->volumeAABBMax = glm::vec4(this->volumeObject->maxPoint, 0.0f);
          shaderDataBuffer->time = time;
          shaderDataBuffer->totalWeight = this->viewportResources->totalWeight;
          shaderDataBuffer->resetBuffer = passData.resetBuffer;
        }
        passData.memoryPool->EndSet();

        std::vector<legit::ImageSamplerBinding> imageSamplerBindings;
        imageSamplerBindings.push_back(shaderDataSetInfo->MakeImageSamplerBinding("specularCubemap", this->specularCubemapView.get(), cubemapSampler.get()));
        imageSamplerBindings.push_back(shaderDataSetInfo->MakeImageSamplerBinding("volumeSampler", this->volumeImageView.get(), volumeSampler.get()));
        imageSamplerBindings.push_back(shaderDataSetInfo->MakeImageSamplerBinding("preintReflectSampler", this->preintReflectImageView.get(), preintSampler.get()));
        imageSamplerBindings.push_back(shaderDataSetInfo->MakeImageSamplerBinding("preintScatterSampler", this->preintScatterImageView.get(), preintSampler.get()));

        auto shaderDataSet = this->core->GetDescriptorSetCache()->GetDescriptorSet(*shaderDataSetInfo, shaderData.uniformBufferBindings, {}, imageSamplerBindings);

        passContext.GetCommandBuffer().bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeineInfo.pipelineLayout, ShaderDataSetIndex, { shaderDataSet }, { shaderData.dynamicOffset });
        passContext.GetCommandBuffer().draw(4, 1, 0, 0);
      }
    }));
    viewportResources->totalWeight += 1.0f;

    core->GetRenderGraph()->AddPass(legit::RenderGraph::RenderPassDesc()
      .SetColorAttachments({ frameInfo.swapchainImageViewProxyId }, vk::AttachmentLoadOp::eClear)
      .SetInputImages(
        {
          viewportResources->accumulatedLightViewProxyId->Id()
        })
      .SetRenderAreaExtent(viewportResources->extent)
      .SetRecordFunc([this, passData](legit::RenderGraph::RenderPassContext passContext)
    {
      auto pipeineInfo = this->core->GetPipelineCache()->BindGraphicsPipeline(passContext.GetCommandBuffer(), passContext.GetRenderPass()->GetHandle(), legit::DepthSettings::Disabled(), { legit::BlendSettings::Opaque() }, legit::VertexDeclaration(), vk::PrimitiveTopology::eTriangleFan, finalGathererShader.program.get());
      {
        const legit::DescriptorSetLayoutKey *shaderDataSetInfo = finalGathererShader.fragment->GetSetInfo(ShaderDataSetIndex);
        auto shaderData = passData.memoryPool->BeginSet(shaderDataSetInfo);
        {
          auto shaderDataBuffer = passData.memoryPool->GetUniformBufferData<FinalGathererShader::DataBuffer>("FinalGathererData");

          shaderDataBuffer->viewportExtent = glm::vec4(viewportResources->extent.width, viewportResources->extent.height, 0.0f, 0.0f);
          shaderDataBuffer->totalWeight = 1.0f;
        }
        passData.memoryPool->EndSet();

        std::vector<legit::ImageSamplerBinding> imageSamplerBindings;
        auto accumulatedLightImageView = passContext.GetImageView(this->viewportResources->accumulatedLightViewProxyId->Id());
        imageSamplerBindings.push_back(shaderDataSetInfo->MakeImageSamplerBinding("accumulatedLight", accumulatedLightImageView, screenspaceSampler.get()));

        auto shaderDataSet = this->core->GetDescriptorSetCache()->GetDescriptorSet(*shaderDataSetInfo, shaderData.uniformBufferBindings, {}, imageSamplerBindings);

        passContext.GetCommandBuffer().bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeineInfo.pipelineLayout, ShaderDataSetIndex, { shaderDataSet }, { shaderData.dynamicOffset });
        passContext.GetCommandBuffer().draw(4, 1, 0, 0);
      }
    }));
  }

  void ReloadShaders()
  {
    volumeRendererShader.vertex.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/Common/screenspaceQuad.vert.spv"));
    volumeRendererShader.fragment.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/VolumeRenderer/volumeRenderer.frag.spv"));
    volumeRendererShader.program.reset(new legit::ShaderProgram(volumeRendererShader.vertex.get(), volumeRendererShader.fragment.get()));

    finalGathererShader.vertex.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/Common/screenspaceQuad.vert.spv"));
    finalGathererShader.fragment.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/VolumeRenderer/finalGatherer.frag.spv"));
    finalGathererShader.program.reset(new legit::ShaderProgram(finalGathererShader.vertex.get(), finalGathererShader.fragment.get()));
  }
private:
  const static uint32_t ShaderDataSetIndex = 0;
  const static uint32_t DrawCallDataSetIndex = 1;

  legit::VertexDeclaration vertexDecl;

  struct ViewportResources
  {
    ViewportResources(legit::RenderGraph *renderGraph, vk::Extent2D extent, legit::ImageView *accumulatedLightView)
    {
      this->extent = extent;
      glm::uvec2 screenSize = { extent.width, extent.height };

      accumulatedLightViewProxyId = renderGraph->AddExternalImageView(accumulatedLightView, legit::ImageUsageTypes::GraphicsShaderRead);
    }
    legit::RenderGraph::ImageViewProxyUnique accumulatedLightViewProxyId;
    vk::Extent2D extent;
    float totalWeight;
  };


  struct VolumeRendererShader
  {
#pragma pack(push, 1)
    struct DataBuffer
    {
      glm::mat4 viewMatrix;
      glm::mat4 projMatrix;
      glm::vec4 viewportExtent;
      glm::vec4 volumeAABBMin;
      glm::vec4 volumeAABBMax;
      float time;
      float totalWeight;
      float resetBuffer;
    };
#pragma pack(pop)

    std::unique_ptr<legit::Shader> vertex;
    std::unique_ptr<legit::Shader> fragment;
    std::unique_ptr<legit::ShaderProgram> program;
  } volumeRendererShader;

  struct FinalGathererShader
  {
#pragma pack(push, 1)
    struct DataBuffer
    {
      glm::vec4 viewportExtent;
      float totalWeight;
    };
#pragma pack(pop)

    std::unique_ptr<legit::Shader> vertex;
    std::unique_ptr<legit::Shader> fragment;
    std::unique_ptr<legit::ShaderProgram> program;
  } finalGathererShader;

  std::unique_ptr<ViewportResources> viewportResources;
  //std::vector<FrameResources> frameResourcesDatum;
  std::unique_ptr<legit::Image> accumulatedLight;
  std::unique_ptr<legit::ImageView> accumulatedLightView;

  std::unique_ptr<legit::Sampler> screenspaceSampler;
  std::unique_ptr<legit::Sampler> cubemapSampler;
  std::unique_ptr<legit::Sampler> volumeSampler;
  std::unique_ptr<legit::Sampler> preintSampler;

  std::unique_ptr<legit::Image> specularCubemap;
  std::unique_ptr<legit::ImageView> specularCubemapView;
  std::unique_ptr<legit::Image> diffuseCubemap;
  std::unique_ptr<legit::ImageView> diffuseCubemapView;

  std::unique_ptr<VolumeObject> volumeObject;
  std::unique_ptr<legit::Image> volumeImage;
  std::unique_ptr<legit::ImageView> volumeImageView;

  std::unique_ptr<legit::Image> preintReflectImage;
  std::unique_ptr<legit::ImageView> preintReflectImageView;

  std::unique_ptr<legit::Image> preintScatterImage;
  std::unique_ptr<legit::ImageView> preintScatterImageView;

  float totalWeight = 0.0f;
  bool viewChanged;

  legit::Core *core;

};