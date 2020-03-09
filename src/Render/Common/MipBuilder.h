#pragma once

struct UnmippedProxy
{
  UnmippedProxy(legit::RenderGraph *renderGraph, vk::Format format, glm::uvec2 _baseSize, vk::ImageUsageFlags usageFlags) :
    baseSize(_baseSize)
  {
    imageProxy = renderGraph->AddImage(format, 1, 1, _baseSize, usageFlags);
    imageViewProxy = renderGraph->AddImageView(imageProxy->Id(), 0, 1, 0, 1);
  }
  legit::RenderGraph::ImageProxyUnique imageProxy;
  legit::RenderGraph::ImageViewProxyUnique imageViewProxy;
  glm::uvec2 baseSize;
};

struct MippedProxy
{
  MippedProxy(legit::RenderGraph *renderGraph, vk::Format format, glm::uvec2 _baseSize, vk::ImageUsageFlags usageFlags) :
    baseSize(_baseSize)
  {
    uint32_t mipsCount = 10;
    imageProxy = renderGraph->AddImage(format, mipsCount, 1, _baseSize, usageFlags);
    imageViewProxy = renderGraph->AddImageView(imageProxy->Id(), 0, mipsCount, 0, 1);
    for (uint32_t mipIndex = 0; mipIndex < mipsCount; mipIndex++)
    {
      mipImageViewProxies.push_back(renderGraph->AddImageView(imageProxy->Id(), mipIndex, 1, 0, 1));
    }
  }
  legit::RenderGraph::ImageProxyUnique imageProxy;
  legit::RenderGraph::ImageViewProxyUnique imageViewProxy;
  std::vector<legit::RenderGraph::ImageViewProxyUnique> mipImageViewProxies;
  glm::uvec2 baseSize;
};

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

struct PersistentVolumeProxy
{
  PersistentVolumeProxy(legit::Core *core, vk::Format format, glm::uvec3 _baseSize, vk::ImageUsageFlags usageFlags, legit::ImageUsageTypes baseUsage) :
    baseSize(_baseSize)
  {
    auto volumeCreateDesc = legit::Image::CreateInfoVolume(_baseSize, 1, 1, format, usageFlags);
    this->volumeImage = std::unique_ptr<legit::Image>(new legit::Image(core->GetPhysicalDevice(), core->GetLogicalDevice(), volumeCreateDesc));

    legit::ExecuteOnceQueue transferQueue(core);
    auto transferCommandBuffer = transferQueue.BeginCommandBuffer();
    {
      AddTransitionBarrier(volumeImage->GetImageData(), legit::ImageUsageTypes::Unknown, baseUsage, transferCommandBuffer);
    }
    transferQueue.EndCommandBuffer();

    //legit::LoadTexelData(core, &resObject.volumeData, volumeImage->GetImageData());
    this->imageView = std::unique_ptr<legit::ImageView>(new legit::ImageView(core->GetLogicalDevice(), volumeImage->GetImageData(), 0, 1, 0, 1));
    this->imageViewProxy = core->GetRenderGraph()->AddExternalImageView(imageView.get(), baseUsage);
  }
  //legit::RenderGraph::ImageProxyUnique imageProxy;
  std::unique_ptr<legit::Image> volumeImage;

  legit::RenderGraph::ImageViewProxyUnique imageViewProxy;
  std::unique_ptr<legit::ImageView> imageView;

  glm::uvec2 baseSize;
};


struct PersistentMippedVolumeProxy
{
  PersistentMippedVolumeProxy(legit::Core *core, vk::Format format, glm::uvec3 _baseSize, vk::ImageUsageFlags usageFlags, legit::ImageUsageTypes baseUsage) :
    baseSize(_baseSize)
  {
    uint32_t mipsCount = 1;
    uint32_t currSize = _baseSize.x;
    while (currSize > 1)
    {
      mipsCount++;
      currSize /= 2;
    }
    auto volumeCreateDesc = legit::Image::CreateInfoVolume(_baseSize, mipsCount, 1, format, usageFlags);
    this->volumeImage = std::unique_ptr<legit::Image>(new legit::Image(core->GetPhysicalDevice(), core->GetLogicalDevice(), volumeCreateDesc));
    legit::ExecuteOnceQueue transferQueue(core);
    auto transferCommandBuffer = transferQueue.BeginCommandBuffer();
    {
      AddTransitionBarrier(volumeImage->GetImageData(), legit::ImageUsageTypes::Unknown, baseUsage, transferCommandBuffer);
    }
    transferQueue.EndCommandBuffer();

    //legit::LoadTexelData(core, &resObject.volumeData, volumeImage->GetImageData());
    this->mippedImageView = std::unique_ptr<legit::ImageView>(new legit::ImageView(core->GetLogicalDevice(), volumeImage->GetImageData(), 0, mipsCount, 0, 1));
    this->mippedImageViewProxy = core->GetRenderGraph()->AddExternalImageView(mippedImageView.get(), baseUsage);

    for (uint32_t mipIndex = 0; mipIndex < mipsCount; mipIndex++)
    {
      Mip mip;
      mip.volumeImageView = std::unique_ptr<legit::ImageView>(new legit::ImageView(core->GetLogicalDevice(), volumeImage->GetImageData(), mipIndex, 1, 0, 1));
      mip.imageViewProxy = core->GetRenderGraph()->AddExternalImageView(mip.volumeImageView.get(), baseUsage);
      mips.emplace_back(std::move(mip));
    }
  }
  //legit::RenderGraph::ImageProxyUnique imageProxy;
  std::unique_ptr<legit::Image> volumeImage;

  legit::RenderGraph::ImageViewProxyUnique mippedImageViewProxy;
  std::unique_ptr<legit::ImageView> mippedImageView;

  struct Mip
  {
    legit::RenderGraph::ImageViewProxyUnique imageViewProxy;
    std::unique_ptr<legit::ImageView> volumeImageView;
  };
  std::vector<Mip> mips;

  glm::uvec3 baseSize;
};

class MipBuilder
{
public:
  MipBuilder(legit::Core *_core)
  {
    this->core = _core;

    imageSpaceSampler.reset(new legit::Sampler(core->GetLogicalDevice(), vk::SamplerAddressMode::eClampToEdge, vk::Filter::eNearest, vk::SamplerMipmapMode::eNearest));
    ReloadShaders();
  }
  enum struct FilterTypes
  {
    Avg,
    Depth
  };

  void BuildMips(legit::RenderGraph *renderGraph, legit::ShaderMemoryPool *memoryPool, const MippedProxy &mippedProxy, FilterTypes filterType = FilterTypes::Avg)
  {
    vk::Extent2D layerSize(mippedProxy.baseSize.x, mippedProxy.baseSize.y);

    for (size_t mipIndex = 1; mipIndex < mippedProxy.mipImageViewProxies.size(); mipIndex++)
    {
      layerSize.width /= 2;
      layerSize.height /= 2;

      if (layerSize.width <= 0 || layerSize.height <= 0)
        break;
      auto srcProxyId = mippedProxy.mipImageViewProxies[mipIndex - 1]->Id();
      auto dstProxyId = mippedProxy.mipImageViewProxies[mipIndex]->Id();
      renderGraph->AddPass(legit::RenderGraph::RenderPassDesc()
        .SetColorAttachments({ dstProxyId })
        .SetInputImages({ srcProxyId })
        .SetRenderAreaExtent(layerSize)
        .SetProfilerInfo(legit::Colors::nephritis, "MipBuilderPass")
        .SetRecordFunc([this, memoryPool, srcProxyId, mipIndex, filterType](legit::RenderGraph::RenderPassContext passContext)
      {
        auto pipeineInfo = this->core->GetPipelineCache()->BindGraphicsPipeline(passContext.GetCommandBuffer(), passContext.GetRenderPass()->GetHandle(), legit::DepthSettings::Disabled(), { legit::BlendSettings::Opaque() }, legit::VertexDeclaration(), vk::PrimitiveTopology::eTriangleFan, mipLevelBuilder.shaderProgram.get());
        {
          const legit::DescriptorSetLayoutKey *shaderDataSetInfo = mipLevelBuilder.fragmentShader->GetSetInfo(ShaderDataSetIndex);
          auto dynamicUniformBindings = memoryPool->BeginSet(shaderDataSetInfo);
          {
            auto shaderDataBuffer = memoryPool->GetUniformBufferData<MipLevelBuilder::ShaderDataBuffer>("MipLevelBuilderData");
            shaderDataBuffer->filterType = (filterType == FilterTypes::Avg) ? 0.0f : 1.0f;
          }
          memoryPool->EndSet();

          std::vector<legit::ImageSamplerBinding> imageSamplerBindings;
          auto prevMipView = passContext.GetImageView(srcProxyId);
          imageSamplerBindings.push_back(shaderDataSetInfo->MakeImageSamplerBinding("prevLevelSampler", prevMipView, imageSpaceSampler.get()));
          auto shaderDataSet = this->core->GetDescriptorSetCache()->GetDescriptorSet(*shaderDataSetInfo, dynamicUniformBindings.uniformBufferBindings, {}, imageSamplerBindings);
          passContext.GetCommandBuffer().bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeineInfo.pipelineLayout, ShaderDataSetIndex, { shaderDataSet }, { dynamicUniformBindings.dynamicOffset });
          passContext.GetCommandBuffer().draw(4, 1, 0, 0);
        }
      }));
    }
  }


  void BuildMips(legit::RenderGraph *renderGraph, legit::ShaderMemoryPool *memoryPool, const PersistentMippedVolumeProxy &mippedProxy, FilterTypes filterType = FilterTypes::Avg)
  {
    glm::uvec3 mipSize = mippedProxy.baseSize;

    for (size_t mipIndex = 1; mipIndex < mippedProxy.mips.size(); mipIndex++)
    {
      mipSize /= 2;

      if (mipSize.x <= 0 || mipSize.y <= 0 || mipSize.z <= 0)
        break;
      auto srcProxyId = mippedProxy.mips[mipIndex - 1].imageViewProxy->Id();
      auto dstProxyId = mippedProxy.mips[mipIndex].imageViewProxy->Id();
      renderGraph->AddPass(legit::RenderGraph::ComputePassDesc()
        .SetStorageImages({ srcProxyId, dstProxyId })
        .SetProfilerInfo(legit::Colors::nephritis, "MipBuilderPass")
        .SetRecordFunc([this, memoryPool, srcProxyId, dstProxyId, mipSize, filterType](legit::RenderGraph::PassContext passContext)
      {
        auto shader = this->volumeMipBuilderShader.compute.get();
        auto pipeineInfo = this->core->GetPipelineCache()->BindComputePipeline(passContext.GetCommandBuffer(), shader);
        {
          const legit::DescriptorSetLayoutKey *shaderDataSetInfo = shader->GetSetInfo(ShaderDataSetIndex);
          auto shaderDataSetUniforms = memoryPool->BeginSet(shaderDataSetInfo);
          {
            auto mappedShaderDataBuf= memoryPool->GetUniformBufferData<VolumeMipLevelBuilder::ShaderDataBuffer>("ShaderDataBuffer");
            mappedShaderDataBuf->dstMipSize = glm::uvec4(mipSize, 0.0f);
          }
          memoryPool->EndSet();

          std::vector<legit::StorageImageBinding> storageImageBindings;
          auto srcMipView = passContext.GetImageView(srcProxyId);
          storageImageBindings.push_back(shaderDataSetInfo->MakeStorageImageBinding("srcMipImage", srcMipView));
          auto dstMipView = passContext.GetImageView(dstProxyId);
          storageImageBindings.push_back(shaderDataSetInfo->MakeStorageImageBinding("dstMipImage", dstMipView));

          auto shaderDataSetBindings = legit::DescriptorSetBindings()
            .SetUniformBufferBindings(shaderDataSetUniforms.uniformBufferBindings)
            .SetStorageImageBindings(storageImageBindings);

          auto shaderDataSet = this->core->GetDescriptorSetCache()->GetDescriptorSet(*shaderDataSetInfo, shaderDataSetBindings);
          passContext.GetCommandBuffer().bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipeineInfo.pipelineLayout, ShaderDataSetIndex, { shaderDataSet }, { shaderDataSetUniforms.dynamicOffset });

          glm::uvec3 workGroupSize = shader->GetLocalSize();
          glm::uvec3 groupsCount = (mipSize + workGroupSize - glm::uvec3(1)) / workGroupSize;
          passContext.GetCommandBuffer().dispatch(uint32_t(groupsCount.x), uint32_t(groupsCount.y), uint32_t(groupsCount.z));
        }
      }));
    }
  }

  void ReloadShaders()
  {
    mipLevelBuilder.vertexShader.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/Common/screenspaceQuad.vert.spv"));
    mipLevelBuilder.fragmentShader.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/Common/mipLevelBuilder.frag.spv"));
    mipLevelBuilder.shaderProgram.reset(new legit::ShaderProgram(mipLevelBuilder.vertexShader.get(), mipLevelBuilder.fragmentShader.get()));

    volumeMipBuilderShader.compute.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/Common/volumeMipBuilder.comp.spv"));
  }
private:


  const static uint32_t ShaderDataSetIndex = 0;
  const static uint32_t DrawCallDataSetIndex = 1;

  struct MipLevelBuilder
  {
#pragma pack(push, 1)
    struct ShaderDataBuffer
    {
      float filterType;
    };
#pragma pack(pop)

    std::unique_ptr<legit::Shader> vertexShader;
    std::unique_ptr<legit::Shader> fragmentShader;
    std::unique_ptr<legit::ShaderProgram> shaderProgram;
  } mipLevelBuilder;

  struct VolumeMipLevelBuilder
  {
    #pragma pack(push, 1)
    struct ShaderDataBuffer
    {
      glm::uvec4 dstMipSize;
    };
    #pragma pack(pop)
    std::unique_ptr<legit::Shader> compute;
  } volumeMipBuilderShader;

  std::unique_ptr<legit::Sampler> imageSpaceSampler;

  legit::Core *core;
};

