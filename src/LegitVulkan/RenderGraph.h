#include "RenderPassCache.h"

namespace legit
{
  class RenderGraph;

  template<typename Base>
  struct RenderGraphProxyId
  {
    RenderGraphProxyId() :
      id(size_t(-1))
    {
    }

    bool operator ==(const RenderGraphProxyId<Base> &other) const
    {
      return this->id == other.id;
    }
    bool IsValid()
    {
      return id != size_t(-1);
    }
  private:
    explicit RenderGraphProxyId(size_t _id) : id(_id) {}
    size_t id;
    friend class RenderGraph;
  };

  class ImageCache
  {
  public:
    ImageCache(vk::PhysicalDevice _physicalDevice, vk::Device _logicalDevice) : physicalDevice(_physicalDevice), logicalDevice(_logicalDevice)
    {}

    struct ImageKey
    {
      bool operator < (const ImageKey &other) const
      {
        auto lUsageFlags = VkMemoryMapFlags(usageFlags);
        auto rUsageFlags = VkMemoryMapFlags(other.usageFlags);
        return std::tie(format, mipsCount, arrayLayersCount, lUsageFlags, size.x, size.y, size.z) < std::tie(other.format, other.mipsCount, other.arrayLayersCount, rUsageFlags, other.size.x, other.size.y, other.size.z);
      }
      vk::Format format;
      vk::ImageUsageFlags usageFlags;
      uint32_t mipsCount;
      uint32_t arrayLayersCount;
      glm::uvec3 size;
    };

    void Release()
    {
      for (auto &cacheEntry : imageCache)
      {
        cacheEntry.second.usedCount = 0;
      }
    }

    legit::ImageData *GetImage(ImageKey imageKey)
    {
      auto &cacheEntry = imageCache[imageKey];
      if (cacheEntry.usedCount + 1 > cacheEntry.images.size())
      {
        vk::ImageCreateInfo imageCreateInfo;
        if(imageKey.size.z == glm::u32(-1))
          imageCreateInfo = legit::Image::CreateInfo2d(glm::uvec2(imageKey.size.x, imageKey.size.y), imageKey.mipsCount, imageKey.arrayLayersCount, imageKey.format, imageKey.usageFlags);
        else
          imageCreateInfo = legit::Image::CreateInfoVolume(imageKey.size, imageKey.mipsCount, imageKey.arrayLayersCount, imageKey.format, imageKey.usageFlags);
        auto newImage = std::unique_ptr<legit::Image>(new legit::Image(physicalDevice, logicalDevice, imageCreateInfo));
        cacheEntry.images.emplace_back(std::move(newImage));
      }
      return cacheEntry.images[cacheEntry.usedCount++]->GetImageData();
    }
  private:
    struct ImageCacheEntry
    {
      ImageCacheEntry() : usedCount(0) {}
      std::vector<std::unique_ptr<legit::Image> > images;
      size_t usedCount;
    };
    std::map<ImageKey, ImageCacheEntry> imageCache;
    vk::PhysicalDevice physicalDevice;
    vk::Device logicalDevice;
  };

  class ImageViewCache
  {
  public:
    ImageViewCache(vk::PhysicalDevice _physicalDevice, vk::Device _logicalDevice) : physicalDevice(_physicalDevice), logicalDevice(_logicalDevice)
    {}
    struct ImageViewKey
    {
      legit::ImageData *image;
      ImageSubresourceRange subresourceRange;
      bool operator < (const ImageViewKey &other) const
      {
        return std::tie(image, subresourceRange) < std::tie(other.image, other.subresourceRange);
      }
    };

    legit::ImageView *GetImageView(ImageViewKey imageViewKey)
    {
      auto &imageView = imageViewCache[imageViewKey];
      if (!imageView)
        imageView = std::unique_ptr<legit::ImageView>(new legit::ImageView(
          logicalDevice,
          imageViewKey.image,
          imageViewKey.subresourceRange.baseMipLevel,
          imageViewKey.subresourceRange.mipsCount,
          imageViewKey.subresourceRange.baseArrayLayer,
          imageViewKey.subresourceRange.arrayLayersCount));
      return imageView.get();
    }
  private:
    std::map<ImageViewKey, std::unique_ptr<legit::ImageView> > imageViewCache;
    vk::PhysicalDevice physicalDevice;
    vk::Device logicalDevice;
  };




  class BufferCache
  {
  public:
    BufferCache(vk::PhysicalDevice _physicalDevice, vk::Device _logicalDevice) : physicalDevice(_physicalDevice), logicalDevice(_logicalDevice)
    {}

    struct BufferKey
    {
      uint32_t elementSize;
      uint32_t elementsCount;
      bool operator < (const BufferKey &other) const
      {
        return std::tie(elementSize, elementsCount) < std::tie(other.elementSize, other.elementsCount);
      }
    };

    void Release()
    {
      for (auto &cacheEntry : bufferCache)
      {
        cacheEntry.second.usedCount = 0;
      }
    }

    legit::Buffer *GetBuffer(BufferKey bufferKey)
    {
      auto &cacheEntry = bufferCache[bufferKey];
      if (cacheEntry.usedCount + 1 > cacheEntry.buffers.size())
      {
        auto newBuffer = std::unique_ptr<legit::Buffer>(new legit::Buffer(
          physicalDevice,
          logicalDevice,
          bufferKey.elementSize * bufferKey.elementsCount,
          vk::BufferUsageFlagBits::eStorageBuffer,
          vk::MemoryPropertyFlagBits::eDeviceLocal));
        cacheEntry.buffers.emplace_back(std::move(newBuffer));
      }
      return cacheEntry.buffers[cacheEntry.usedCount++].get();
    }
  private:
    struct BufferCacheEntry
    {
      BufferCacheEntry() : usedCount(0) {}
      std::vector<std::unique_ptr<legit::Buffer> > buffers;
      size_t usedCount;
    };
    std::map<BufferKey, BufferCacheEntry> bufferCache;
    vk::PhysicalDevice physicalDevice;
    vk::Device logicalDevice;
  };


  class RenderGraph
  {
  private:
    struct ImageProxy;
    using ImageProxyPool = Utils::Pool<ImageProxy>;

    struct ImageViewProxy;
    using ImageViewProxyPool = Utils::Pool<ImageViewProxy>;

    struct BufferProxy;
    using BufferProxyPool = Utils::Pool<BufferProxy>;
  public:
    using ImageProxyId = ImageProxyPool::Id;
    using ImageViewProxyId = ImageViewProxyPool::Id;
    using BufferProxyId = BufferProxyPool::Id;

    struct ImageHandleInfo
    {
      ImageHandleInfo() {}
      ImageHandleInfo(RenderGraph *renderGraph, ImageProxyId imageProxyId)
      {
        this->renderGraph = renderGraph;
        this->imageProxyId = imageProxyId;
      }
      void Reset()
      {
        renderGraph->DeleteImage(imageProxyId);
      }
      ImageProxyId Id() const
      {
        return imageProxyId;
      }
    private:
      RenderGraph *renderGraph;
      ImageProxyId imageProxyId;
      friend class RenderGraph;
    };

    struct ImageViewHandleInfo
    {
      ImageViewHandleInfo() {}
      ImageViewHandleInfo(RenderGraph *renderGraph, ImageViewProxyId imageViewProxyId)
      {
        this->renderGraph = renderGraph;
        this->imageViewProxyId = imageViewProxyId;
      }
      void Reset()
      {
        renderGraph->DeleteImageView(imageViewProxyId);
      }
      ImageViewProxyId Id() const
      {
        return imageViewProxyId;
      }
    private:
      RenderGraph *renderGraph;
      ImageViewProxyId imageViewProxyId;
      friend class RenderGraph;
    };
    struct BufferHandleInfo
    {
      BufferHandleInfo() {}
      BufferHandleInfo(RenderGraph *renderGraph, BufferProxyId bufferProxyId)
      {
        this->renderGraph = renderGraph;
        this->bufferProxyId = bufferProxyId;
      }
      void Reset()
      {
        renderGraph->DeleteBuffer(bufferProxyId);
      }
      BufferProxyId Id() const
      {
        return bufferProxyId;
      }
    private:
      RenderGraph *renderGraph;
      BufferProxyId bufferProxyId;
      friend class RenderGraph;
    };

  public:
    RenderGraph(vk::PhysicalDevice _physicalDevice, vk::Device _logicalDevice) :
      physicalDevice(_physicalDevice),
      logicalDevice(_logicalDevice),
      renderPassCache(_logicalDevice),
      framebufferCache(_logicalDevice),
      imageCache(_physicalDevice, _logicalDevice),
      imageViewCache(_physicalDevice, _logicalDevice),
      bufferCache(_physicalDevice, _logicalDevice)
    {
    }

    using ImageProxyUnique = UniqueHandle<ImageHandleInfo, RenderGraph>;
    using ImageViewProxyUnique = UniqueHandle<ImageViewHandleInfo, RenderGraph>;
    using BufferProxyUnique = UniqueHandle<BufferHandleInfo, RenderGraph>;


    ImageProxyUnique AddImage(vk::Format format, uint32_t mipsCount, uint32_t arrayLayersCount, glm::uvec2 size, vk::ImageUsageFlags usageFlags)
    {
      return AddImage(format, mipsCount, arrayLayersCount, glm::uvec3(size.x, size.y, -1), usageFlags);
    }
    ImageProxyUnique AddImage(vk::Format format, uint32_t mipsCount, uint32_t arrayLayersCount, glm::uvec3 size, vk::ImageUsageFlags usageFlags)
    {
      ImageProxy imageProxy;
      imageProxy.type = ImageProxy::Types::Transient;
      imageProxy.imageKey.format = format;
      imageProxy.imageKey.usageFlags = usageFlags;
      imageProxy.imageKey.mipsCount = mipsCount;
      imageProxy.imageKey.arrayLayersCount = arrayLayersCount;
      imageProxy.imageKey.size = size;
      imageProxy.externalImage = nullptr;
      return ImageProxyUnique(ImageHandleInfo(this, imageProxies.Add(std::move(imageProxy))));
    }
    ImageProxyUnique AddExternalImage(legit::ImageData *image)
    {
      ImageProxy imageProxy;
      imageProxy.type = ImageProxy::Types::External;
      imageProxy.externalImage = image;
      return ImageProxyUnique(ImageHandleInfo(this, imageProxies.Add(std::move(imageProxy))));
    }
    ImageViewProxyUnique AddImageView(ImageProxyId imageProxyId, uint32_t baseMipLevel, uint32_t mipLevelsCount, uint32_t baseArrayLayer, uint32_t arrayLayersCount)
    {
      ImageViewProxy imageViewProxy;
      imageViewProxy.externalView = nullptr;
      imageViewProxy.type = ImageViewProxy::Types::Transient;
      imageViewProxy.imageProxyId = imageProxyId;
      imageViewProxy.subresourceRange.baseMipLevel = baseMipLevel;
      imageViewProxy.subresourceRange.mipsCount = mipLevelsCount;
      imageViewProxy.subresourceRange.baseArrayLayer = baseArrayLayer;
      imageViewProxy.subresourceRange.arrayLayersCount = arrayLayersCount;
      return ImageViewProxyUnique(ImageViewHandleInfo(this, imageViewProxies.Add(std::move(imageViewProxy))));
    }
    ImageViewProxyUnique AddExternalImageView(legit::ImageView *imageView, legit::ImageUsageTypes usageType = legit::ImageUsageTypes::Unknown)
    {
      ImageViewProxy imageViewProxy;
      imageViewProxy.externalView = imageView;
      imageViewProxy.externalUsageType = usageType;
      imageViewProxy.type = ImageViewProxy::Types::External;
      imageViewProxy.imageProxyId = ImageProxyId();
      return ImageViewProxyUnique(ImageViewHandleInfo(this, imageViewProxies.Add(std::move(imageViewProxy))));
    }
  private:
    void DeleteImage(ImageProxyId imageId)
    {
      imageProxies.Release(imageId);
    }

    void DeleteImageView(ImageViewProxyId imageViewId)
    {
      imageViewProxies.Release(imageViewId);
    }

    void DeleteBuffer(BufferProxyId bufferId)
    {
      bufferProxies.Release(bufferId);
    }
  public:

    glm::uvec2 GetMipSize(ImageProxyId imageProxyId, uint32_t mipLevel)
    {
      auto &imageProxy = imageProxies.Get(imageProxyId);
      switch (imageProxy.type)
      {
        case ImageProxy::Types::External:
        {
          return imageProxy.externalImage->GetMipSize(mipLevel);
        }break;
        case ImageProxy::Types::Transient:
        {
          glm::u32 mipMult = (1 << mipLevel);
          return glm::uvec2(imageProxy.imageKey.size.x / mipMult, imageProxy.imageKey.size.y / mipMult);
        }break;
      }
      return glm::uvec2(-1, -1);
    }

    glm::uvec2 GetMipSize(ImageViewProxyId imageViewProxyId, uint32_t mipOffset)
    {
      auto &imageViewProxy = imageViewProxies.Get(imageViewProxyId);
      switch (imageViewProxy.type)
      {
        case ImageViewProxy::Types::External:
        {
          uint32_t mipLevel = imageViewProxy.externalView->GetBaseMipLevel() + mipOffset;
          return imageViewProxy.externalView->GetImageData()->GetMipSize(mipLevel);
        }break;
        case ImageViewProxy::Types::Transient:
        {
          uint32_t mipLevel = imageViewProxy.subresourceRange.baseMipLevel + mipOffset;
          return GetMipSize(imageViewProxy.imageProxyId, mipLevel);
        }break;
      }
      return glm::uvec2(-1, -1);
    }


    template<typename BufferType>
    BufferProxyUnique AddBuffer(uint32_t count)
    {
      BufferProxy bufferProxy;
      bufferProxy.type = BufferProxy::Types::Transient;
      bufferProxy.bufferKey.elementSize = sizeof(BufferType);
      bufferProxy.bufferKey.elementsCount = count;
      bufferProxy.externalBuffer = nullptr;
      return BufferProxyUnique(BufferHandleInfo(this, bufferProxies.Add(std::move(bufferProxy))));
    }

    BufferProxyUnique AddExternalBuffer(legit::Buffer *buffer)
    {
      BufferProxy bufferProxy;
      bufferProxy.type = BufferProxy::Types::External;
      bufferProxy.bufferKey.elementSize = -1;
      bufferProxy.bufferKey.elementsCount = -1;
      bufferProxy.externalBuffer = buffer;
      return BufferProxyUnique(BufferHandleInfo(this, bufferProxies.Add(std::move(bufferProxy))));
    }

    struct PassContext
    {
      legit::ImageView *GetImageView(ImageViewProxyId imageViewProxyId)
      {
        return resolvedImageViews[imageViewProxyId.asInt];
      }
      legit::Buffer *GetBuffer(BufferProxyId bufferProxy)
      {
        return resolvedBuffers[bufferProxy.asInt];
      }
      vk::CommandBuffer GetCommandBuffer()
      {
        return commandBuffer;
      }
    private:
      std::vector<legit::ImageView *> resolvedImageViews;
      std::vector<legit::Buffer *> resolvedBuffers;
      vk::CommandBuffer commandBuffer;
      friend class RenderGraph;
    };

    struct RenderPassContext : public PassContext
    {
      legit::RenderPass *GetRenderPass()
      {
        return renderPass;
      }
    private:
      legit::RenderPass *renderPass;
      friend class RenderGraph;
    };

    struct RenderPassDesc
    {
      RenderPassDesc()
      {
        profilerTaskName = "RenderPass";
        profilerTaskColor = glm::packUnorm4x8(glm::vec4(1.0f, 0.5f, 0.0f, 1.0f));
      }
      struct Attachment
      {
        ImageViewProxyId imageViewProxyId;
        vk::AttachmentLoadOp loadOp;
        vk::ClearValue clearValue;
      };
      RenderPassDesc &SetColorAttachments(
        const std::vector<ImageViewProxyId> &_colorAttachmentViewProxies, 
        vk::AttachmentLoadOp _loadOp = vk::AttachmentLoadOp::eDontCare, 
        vk::ClearValue _clearValue = vk::ClearColorValue(std::array<float, 4>{1.0f, 0.5f, 0.0f, 1.0f}))
      {
        this->colorAttachments.resize(_colorAttachmentViewProxies.size());
        for (size_t index = 0; index < _colorAttachmentViewProxies.size(); index++)
        {
          this->colorAttachments[index] = { _colorAttachmentViewProxies [index], _loadOp, _clearValue};
        }
        return *this;
      }
      RenderPassDesc &SetColorAttachments(std::vector<Attachment> &&_colorAttachments)
      {
        this->colorAttachments = std::move(_colorAttachments);
        return *this;
      }

      RenderPassDesc &SetDepthAttachment(
        ImageViewProxyId _depthAttachmentViewProxyId,
        vk::AttachmentLoadOp _loadOp = vk::AttachmentLoadOp::eDontCare,
        vk::ClearValue _clearValue = vk::ClearDepthStencilValue(1.0f, 0))
      {
        this->depthAttachment.imageViewProxyId = _depthAttachmentViewProxyId;
        this->depthAttachment.loadOp = _loadOp;
        this->depthAttachment.clearValue = _clearValue;
        return *this;
      }
      RenderPassDesc &SetDepthAttachment(Attachment _depthAttachment)
      {
        this->depthAttachment = _depthAttachment;
        return *this;
      }

      RenderPassDesc &SetInputImages(std::vector<ImageViewProxyId> &&_inputImageViewProxies)
      {
        this->inputImageViewProxies = std::move(_inputImageViewProxies);
        return *this;
      }
      RenderPassDesc &SetStorageBuffers(std::vector<BufferProxyId> &&_inoutStorageBufferProxies)
      {
        this->inoutStorageBufferProxies = _inoutStorageBufferProxies;
        return *this;
      }
      RenderPassDesc &SetStorageImages(std::vector<ImageViewProxyId> &&_inoutStorageImageProxies)
      {
        this->inoutStorageImageProxies = _inoutStorageImageProxies;
        return *this;
      }
      RenderPassDesc &SetRenderAreaExtent(vk::Extent2D _renderAreaExtent)
      {
        this->renderAreaExtent = _renderAreaExtent;
        return *this;
      }

      RenderPassDesc &SetRecordFunc(std::function<void(RenderPassContext)> _recordFunc)
      {
        this->recordFunc = _recordFunc;
        return *this;
      }
      RenderPassDesc &SetProfilerInfo(uint32_t taskColor, std::string taskName)
      {
        this->profilerTaskColor = taskColor;
        this->profilerTaskName = taskName;
        return *this;
      }

      std::vector<Attachment> colorAttachments;
      Attachment depthAttachment;

      std::vector<ImageViewProxyId> inputImageViewProxies;
      std::vector<BufferProxyId> inoutStorageBufferProxies;
      std::vector<ImageViewProxyId> inoutStorageImageProxies;

      vk::Extent2D renderAreaExtent;
      std::function<void(RenderPassContext)> recordFunc;

      std::string profilerTaskName;
      uint32_t profilerTaskColor;
    };

    void AddRenderPass(
      std::vector<ImageViewProxyId> colorAttachmentImageProxies,
      ImageViewProxyId depthAttachmentImageProxy,
      std::vector<ImageViewProxyId> inputImageViewProxies,
      vk::Extent2D renderAreaExtent,
      vk::AttachmentLoadOp loadOp,
      std::function<void(RenderPassContext)> recordFunc)
    {
      RenderPassDesc renderPassDesc;
      for (const auto &proxy : colorAttachmentImageProxies)
      {
        RenderPassDesc::Attachment colorAttachment = { proxy, loadOp, vk::ClearColorValue(std::array<float, 4>{0.03f, 0.03f, 0.03f, 1.0f}) };
        renderPassDesc.colorAttachments.push_back(colorAttachment);
      }
      renderPassDesc.depthAttachment = { depthAttachmentImageProxy, loadOp, vk::ClearDepthStencilValue(1.0f, 0) };
      renderPassDesc.inputImageViewProxies = inputImageViewProxies;
      renderPassDesc.inoutStorageBufferProxies = {};
      renderPassDesc.renderAreaExtent = renderAreaExtent;
      renderPassDesc.recordFunc = recordFunc;

      AddPass(renderPassDesc);
    }

    void AddPass(RenderPassDesc &renderPassDesc)
    {
      Task task;
      task.type = Task::Types::RenderPass;
      task.index = renderPassDescs.size();
      AddTask(task);

      renderPassDescs.emplace_back(renderPassDesc);
    }

    void Clear()
    {
      *this = RenderGraph(physicalDevice, logicalDevice);
    }

    struct ComputePassDesc
    {
      ComputePassDesc()
      {
        profilerTaskName = "ComputePass";
        profilerTaskColor = legit::Colors::belizeHole;
      }
      ComputePassDesc &SetInputImages(std::vector<ImageViewProxyId> &&_inputImageViewProxies)
      {
        this->inputImageViewProxies = std::move(_inputImageViewProxies);
        return *this;
      }
      ComputePassDesc &SetStorageBuffers(std::vector<BufferProxyId> &&_inoutStorageBufferProxies)
      {
        this->inoutStorageBufferProxies = _inoutStorageBufferProxies;
        return *this;
      }
      ComputePassDesc &SetStorageImages(std::vector<ImageViewProxyId> &&_inoutStorageImageProxies)
      {
        this->inoutStorageImageProxies = _inoutStorageImageProxies;
        return *this;
      }
      ComputePassDesc &SetRecordFunc(std::function<void(PassContext)> _recordFunc)
      {
        this->recordFunc = _recordFunc;
        return *this;
      }
      ComputePassDesc &SetProfilerInfo(uint32_t taskColor, std::string taskName)
      {
        this->profilerTaskColor = taskColor;
        this->profilerTaskName = taskName;
        return *this;
      }
      std::vector<BufferProxyId> inoutStorageBufferProxies;
      std::vector<ImageViewProxyId> inputImageViewProxies;
      std::vector<ImageViewProxyId> inoutStorageImageProxies;

      std::function<void(PassContext)> recordFunc;

      std::string profilerTaskName;
      uint32_t profilerTaskColor;
    };

    void AddPass(ComputePassDesc &computePassDesc)
    {
      Task task;
      task.type = Task::Types::ComputePass;
      task.index = computePassDescs.size();
      AddTask(task);

      computePassDescs.emplace_back(computePassDesc);
    }
    void AddComputePass(
      std::vector<BufferProxyId> inoutBufferProxies,
      std::vector<ImageViewProxyId> inputImageViewProxies,
      std::function<void(PassContext)> recordFunc)
    {
      ComputePassDesc computePassDesc;
      computePassDesc.inoutStorageBufferProxies = inoutBufferProxies;
      computePassDesc.inputImageViewProxies = inputImageViewProxies;
      computePassDesc.recordFunc = recordFunc;

      AddPass(computePassDesc);
    }

    void AddImageTransfer(legit::Buffer *srcBuffer, ImageViewProxyId dstImageViewId)
    {
      ImageTransferDesc imageTransferDesc;
      imageTransferDesc.srcBuffer = srcBuffer;
      imageTransferDesc.dstImageViewId = dstImageViewId;

      Task task;
      task.type = Task::Types::ImageTransfer;
      task.index = imageTransferDescs.size();
      AddTask(task);
      imageTransferDescs.push_back(imageTransferDesc);
    }

    struct ImagePresentPassDesc
    {
      ImagePresentPassDesc &SetImage(ImageViewProxyId _presentImageViewProxyId)
      {
        this->presentImageViewProxyId = _presentImageViewProxyId;
      }
      ImageViewProxyId presentImageViewProxyId;
    };
    void AddPass(ImagePresentPassDesc &&imagePresentDesc)
    {
      Task task;
      task.type = Task::Types::ImagePresent;
      task.index = imagePresentDescs.size();
      AddTask(task);

      imagePresentDescs.push_back(imagePresentDesc);
    }
    void AddImagePresent(ImageViewProxyId presentImageViewProxyId)
    {
      ImagePresentPassDesc imagePresentDesc;
      imagePresentDesc.presentImageViewProxyId = presentImageViewProxyId;

      AddPass(std::move(imagePresentDesc));
    }

    struct FrameSyncPassDesc
    {
    };
    void AddPass(FrameSyncPassDesc &&frameSyncDesc)
    {
      Task task;
      task.type = Task::Types::FrameSync;
      task.index = frameSyncDescs.size();
      AddTask(task);

      frameSyncDescs.push_back(frameSyncDesc);
    }

    void Execute(vk::CommandBuffer commandBuffer, legit::CpuProfiler *cpuProfiler, legit::GpuProfiler *gpuProfiler)
    {
      ResolveImages();
      ResolveImageViews();
      ResolveBuffers();



      for (size_t taskIndex = 0; taskIndex < tasks.size(); taskIndex++)
      {
        auto &task = tasks[taskIndex];
        switch (task.type)
        {
          case Task::Types::RenderPass:
          {
            auto &renderPassDesc = renderPassDescs[task.index];
            auto profilerTask = CreateProfilerTask(renderPassDesc);
            auto gpuTask = gpuProfiler->StartScopedTask(profilerTask.name, profilerTask.color, vk::PipelineStageFlagBits::eBottomOfPipe);
            auto cpuTask = cpuProfiler->StartScopedTask(profilerTask.name, profilerTask.color);

            RenderPassContext passContext;
            passContext.resolvedImageViews.resize(imageViewProxies.GetSize(), nullptr);
            passContext.resolvedBuffers.resize(bufferProxies.GetSize(), nullptr);

            for (auto &inputImageViewProxy : renderPassDesc.inputImageViewProxies)
            {
              passContext.resolvedImageViews[inputImageViewProxy.asInt] = GetResolvedImageView(taskIndex, inputImageViewProxy);
            }

            for (auto &inoutStorageImageProxy : renderPassDesc.inoutStorageImageProxies)
            {
              passContext.resolvedImageViews[inoutStorageImageProxy.asInt] = GetResolvedImageView(taskIndex, inoutStorageImageProxy);
            }

            for (auto &inoutBufferProxy : renderPassDesc.inoutStorageBufferProxies)
            {
              passContext.resolvedBuffers[inoutBufferProxy.asInt] = GetResolvedBuffer(taskIndex, inoutBufferProxy);
            }

            vk::PipelineStageFlags srcStage;
            vk::PipelineStageFlags dstStage;
            std::vector<vk::ImageMemoryBarrier> imageBarriers;

            for (auto inputImageViewProxy : renderPassDesc.inputImageViewProxies)
            {
              auto imageView = GetResolvedImageView(taskIndex, inputImageViewProxy);
              AddImageTransitionBarriers(imageView, ImageUsageTypes::GraphicsShaderRead, taskIndex, srcStage, dstStage, imageBarriers);
            }

            for (auto &inoutStorageImageProxy : renderPassDesc.inoutStorageImageProxies)
            {
              auto imageView = GetResolvedImageView(taskIndex, inoutStorageImageProxy);
              AddImageTransitionBarriers(imageView, ImageUsageTypes::GraphicsShaderReadWrite, taskIndex, srcStage, dstStage, imageBarriers);
            }

            for (auto colorAttachment : renderPassDesc.colorAttachments)
            {
              auto imageView = GetResolvedImageView(taskIndex, colorAttachment.imageViewProxyId);
              AddImageTransitionBarriers(imageView, ImageUsageTypes::ColorAttachment, taskIndex, srcStage, dstStage, imageBarriers);
            }

            if(!(renderPassDesc.depthAttachment.imageViewProxyId == ImageViewProxyId()))
            {
              auto imageView = GetResolvedImageView(taskIndex, renderPassDesc.depthAttachment.imageViewProxyId);
              AddImageTransitionBarriers(imageView, ImageUsageTypes::DepthAttachment, taskIndex, srcStage, dstStage, imageBarriers);
            }

            std::vector<vk::BufferMemoryBarrier> bufferBarriers;
            for (auto inoutBufferProxy : renderPassDesc.inoutStorageBufferProxies)
            {
              auto storageBuffer = GetResolvedBuffer(taskIndex, inoutBufferProxy);
              AddBufferBarriers(storageBuffer, BufferUsageTypes::GraphicsShaderReadWrite, taskIndex, srcStage, dstStage, bufferBarriers);
            }

            if (imageBarriers.size() > 0 || bufferBarriers.size() > 0)
              commandBuffer.pipelineBarrier(srcStage, dstStage, vk::DependencyFlags(), {}, bufferBarriers, imageBarriers);

            
            std::vector<FramebufferCache::Attachment> colorAttachments;
            FramebufferCache::Attachment depthAttachment;

            legit::RenderPassCache::RenderPassKey renderPassKey;

            for (auto &attachment : renderPassDesc.colorAttachments)
            {
              auto imageView = GetResolvedImageView(taskIndex, attachment.imageViewProxyId);

              renderPassKey.colorAttachmentDescs.push_back({imageView->GetImageData()->GetFormat(), attachment.loadOp, attachment.clearValue });
              colorAttachments.push_back({ imageView, attachment.clearValue });
            }
            bool depthPresent = !(renderPassDesc.depthAttachment.imageViewProxyId == ImageViewProxyId());
            if (depthPresent)
            {
              auto imageView = GetResolvedImageView(taskIndex, renderPassDesc.depthAttachment.imageViewProxyId);

              renderPassKey.depthAttachmentDesc = { imageView->GetImageData()->GetFormat(), renderPassDesc.depthAttachment.loadOp, renderPassDesc.depthAttachment.clearValue };
              depthAttachment = { imageView, renderPassDesc.depthAttachment.clearValue };
            }
            else
            {
              renderPassKey.depthAttachmentDesc.format = vk::Format::eUndefined;
            }

            auto renderPass = renderPassCache.GetRenderPass(renderPassKey);
            passContext.renderPass = renderPass;

            framebufferCache.BeginPass(commandBuffer, colorAttachments, depthPresent ? (&depthAttachment) : nullptr, renderPass, renderPassDesc.renderAreaExtent);
            passContext.commandBuffer = commandBuffer;
            renderPassDesc.recordFunc(passContext);
            framebufferCache.EndPass(commandBuffer);
          }break;
          case Task::Types::ComputePass:
          {
            auto &computePassDesc = computePassDescs[task.index];
            auto profilerTask = CreateProfilerTask(computePassDesc);
            auto gpuTask = gpuProfiler->StartScopedTask(profilerTask.name, profilerTask.color, vk::PipelineStageFlagBits::eBottomOfPipe);
            auto cpuTask = cpuProfiler->StartScopedTask(profilerTask.name, profilerTask.color);

            PassContext passContext;
            passContext.resolvedImageViews.resize(imageViewProxies.GetSize(), nullptr);
            passContext.resolvedBuffers.resize(bufferProxies.GetSize(), nullptr);

            for (auto &inputImageViewProxy : computePassDesc.inputImageViewProxies)
            {
              passContext.resolvedImageViews[inputImageViewProxy.asInt] = GetResolvedImageView(taskIndex, inputImageViewProxy);
            }

            for (auto &inoutBufferProxy : computePassDesc.inoutStorageBufferProxies)
            {
              passContext.resolvedBuffers[inoutBufferProxy.asInt] = GetResolvedBuffer(taskIndex, inoutBufferProxy);
            }

            for (auto &inoutStorageImageProxy : computePassDesc.inoutStorageImageProxies)
            {
              passContext.resolvedImageViews[inoutStorageImageProxy.asInt] = GetResolvedImageView(taskIndex, inoutStorageImageProxy);
            }

            vk::PipelineStageFlags srcStage;
            vk::PipelineStageFlags dstStage;

            std::vector<vk::ImageMemoryBarrier> imageBarriers;
            for (auto inputImageViewProxy : computePassDesc.inputImageViewProxies)
            {
              auto imageView = GetResolvedImageView(taskIndex, inputImageViewProxy);
              AddImageTransitionBarriers(imageView, ImageUsageTypes::ComputeShaderRead, taskIndex, srcStage, dstStage, imageBarriers);
            }

            for (auto &inoutStorageImageProxy : computePassDesc.inoutStorageImageProxies)
            {
              auto imageView = GetResolvedImageView(taskIndex, inoutStorageImageProxy);
              AddImageTransitionBarriers(imageView, ImageUsageTypes::ComputeShaderReadWrite, taskIndex, srcStage, dstStage, imageBarriers);
            }

            std::vector<vk::BufferMemoryBarrier> bufferBarriers;
            for (auto inoutBufferProxy : computePassDesc.inoutStorageBufferProxies)
            {
              auto storageBuffer = GetResolvedBuffer(taskIndex, inoutBufferProxy);
              AddBufferBarriers(storageBuffer, BufferUsageTypes::ComputeShaderReadWrite, taskIndex, srcStage, dstStage, bufferBarriers);
            }

            if (imageBarriers.size() > 0 || bufferBarriers.size() > 0)
              commandBuffer.pipelineBarrier(srcStage, dstStage, vk::DependencyFlags(), {}, bufferBarriers, imageBarriers);

            passContext.commandBuffer = commandBuffer;
            if(computePassDesc.recordFunc)
              computePassDesc.recordFunc(passContext);
          }break;
          case Task::Types::ImageTransfer:
          {

            /*auto imageTransferDesc = imageTransferDescs[task.index];
            //profilerTasks[taskIndex] = CreateProfilerTask(imageTransferDesc);

            auto dstImageView = resolvedImageViews[imageTransferDesc.dstImageView.id];

            vk::PipelineStageFlags srcStage;
            vk::PipelineStageFlags dstStage;
            std::vector<vk::ImageMemoryBarrier> barriers;
            AddImageTransitionBarrier(dstImageView, vk::ImageLayout::eTransferDstOptimal, srcStage, dstStage, barriers);
            if (barriers.size() > 0)
              commandBuffer.pipelineBarrier(srcStage, dstStage, vk::DependencyFlags(), {}, {}, barriers);

            assert(dstImageView->GetMipLevelsCount() == 1);

            uint32_t mipLevel = dstImageView->GetBaseMipLevel();
            auto imageSubresource = vk::ImageSubresourceLayers()
              .setAspectMask(dstImageView->GetImageData()->GetAspectFlags())
              .setMipLevel(mipLevel)
              .setBaseArrayLayer(dstImageView->GetBaseArrayLayer())
              .setLayerCount(dstImageView->GetArrayLayersCount());

            auto levelSize = dstImageView->GetImageData()->GetMipSize(mipLevel);
            auto copyRegion = vk::BufferImageCopy()
              .setBufferOffset(0)
              .setBufferRowLength(0)
              .setBufferImageHeight(0)
              .setImageSubresource(imageSubresource)
              .setImageOffset(vk::Offset3D(0, 0, 0))
              .setImageExtent(vk::Extent3D(levelSize.x, levelSize.y, 1));

            commandBuffer.copyBufferToImage(imageTransferDesc.srcBuffer->GetHandle(), dstImageView->GetImageData()->GetHandle(), vk::ImageLayout::eTransferDstOptimal, { copyRegion });*/
          }break;
          case Task::Types::ImagePresent:
          {
            auto imagePesentDesc = imagePresentDescs[task.index];
            auto profilerTask = CreateProfilerTask(imagePesentDesc);
            auto gpuTask = gpuProfiler->StartScopedTask(profilerTask.name, profilerTask.color, vk::PipelineStageFlagBits::eBottomOfPipe);
            auto cpuTask = cpuProfiler->StartScopedTask(profilerTask.name, profilerTask.color);

            vk::PipelineStageFlags srcStage;
            vk::PipelineStageFlags dstStage;
            std::vector<vk::ImageMemoryBarrier> imageBarriers;
            {
              auto imageView = GetResolvedImageView(taskIndex, imagePesentDesc.presentImageViewProxyId);
              AddImageTransitionBarriers(imageView, ImageUsageTypes::Present, taskIndex, srcStage, dstStage, imageBarriers);
            }

            if (imageBarriers.size() > 0)
              commandBuffer.pipelineBarrier(srcStage, dstStage, vk::DependencyFlags(), {}, {}, imageBarriers);
          }break;
          case Task::Types::FrameSync:
          {
            auto frameSyncDesc = frameSyncDescs[task.index];
            auto profilerTask = CreateProfilerTask(frameSyncDesc);
            auto gpuTask = gpuProfiler->StartScopedTask(profilerTask.name, profilerTask.color, vk::PipelineStageFlagBits::eBottomOfPipe);
            auto cpuTask = cpuProfiler->StartScopedTask(profilerTask.name, profilerTask.color);

            vk::PipelineStageFlags srcStage = vk::PipelineStageFlagBits::eBottomOfPipe;
            vk::PipelineStageFlags dstStage = vk::PipelineStageFlagBits::eTopOfPipe;
            auto memoryBarrier = vk::MemoryBarrier();
            commandBuffer.pipelineBarrier(srcStage, dstStage, vk::DependencyFlags(), { memoryBarrier }, {}, {});
          }break;
        }
      }

      FlushExternalImages(commandBuffer, cpuProfiler, gpuProfiler);

      renderPassDescs.clear();
      imageTransferDescs.clear();
      imagePresentDescs.clear();
      frameSyncDescs.clear();
      tasks.clear();
    }

  private:
    void FlushExternalImages(vk::CommandBuffer commandBuffer, legit::CpuProfiler *cpuProfiler, legit::GpuProfiler *gpuProfiler)
    {
      //need to make sure I don't override the same mip level of an image twice if there's more than view for it

      /*if (tasks.size() == 0) return;
      size_t lastTaskIndex = tasks.size() - 1;

      for (auto &imageViewProxy : this->imageViewProxies)
      {
        if (imageViewProxy.type == ImageViewProxy::Types::External)
        {
          vk::PipelineStageFlags srcStage;
          vk::PipelineStageFlags dstStage;

          std::vector<vk::ImageMemoryBarrier> imageBarriers;
          if (imageViewProxy.externalUsageType != legit::ImageUsageTypes::Unknown)
          {
            AddImageTransitionBarriers(imageViewProxy.externalView, imageViewProxy.externalUsageType, tasks.size(), srcStage, dstStage, imageBarriers);
          }

          if (imageBarriers.size() > 0)
            commandBuffer.pipelineBarrier(srcStage, dstStage, vk::DependencyFlags(), {}, {}, imageBarriers);
        }
      }*/
    }

    bool ImageViewContainsSubresource(legit::ImageView *imageView, legit::ImageData *imageData, uint32_t mipLevel, uint32_t arrayLayer)
    {
      return (
        imageView->GetImageData() == imageData &&
        arrayLayer >= imageView->GetBaseArrayLayer() && arrayLayer < imageView->GetBaseArrayLayer() + imageView->GetArrayLayersCount() &&
        mipLevel >= imageView->GetBaseMipLevel() && mipLevel < imageView->GetBaseMipLevel() + imageView->GetMipLevelsCount());
    }

    ImageUsageTypes GetTaskImageSubresourceUsageType(size_t taskIndex, legit::ImageData *imageData, uint32_t mipLevel, uint32_t arrayLayer)
    {
      Task &task = tasks[taskIndex];
      switch (task.type)
      {
        case Task::Types::RenderPass:
        {
          auto &renderPassDesc = renderPassDescs[task.index];
          for (auto colorAttachment : renderPassDesc.colorAttachments)
          {
            auto attachmentImageView = GetResolvedImageView(taskIndex, colorAttachment.imageViewProxyId);
            if(ImageViewContainsSubresource(attachmentImageView, imageData, mipLevel, arrayLayer))
              return ImageUsageTypes::ColorAttachment;
          }
          if (!(renderPassDesc.depthAttachment.imageViewProxyId == ImageViewProxyId()))
          {
            auto attachmentImageView = GetResolvedImageView(taskIndex, renderPassDesc.depthAttachment.imageViewProxyId);
            if (ImageViewContainsSubresource(attachmentImageView, imageData, mipLevel, arrayLayer))
              return ImageUsageTypes::DepthAttachment;
          }
          for (auto imageViewProxy : renderPassDesc.inputImageViewProxies)
          {
            if(ImageViewContainsSubresource(GetResolvedImageView(taskIndex, imageViewProxy), imageData, mipLevel, arrayLayer))
              return ImageUsageTypes::GraphicsShaderRead;
          }
          for (auto imageViewProxy : renderPassDesc.inoutStorageImageProxies)
          {
            if (ImageViewContainsSubresource(GetResolvedImageView(taskIndex, imageViewProxy), imageData, mipLevel, arrayLayer))
              return ImageUsageTypes::GraphicsShaderReadWrite;
          }
        }break;
        case Task::Types::ComputePass:
        {
          auto &computePassDesc = computePassDescs[task.index];
          for (auto imageViewProxy : computePassDesc.inputImageViewProxies)
          {
            if (ImageViewContainsSubresource(GetResolvedImageView(taskIndex, imageViewProxy), imageData, mipLevel, arrayLayer))
              return ImageUsageTypes::ComputeShaderRead;
          }
          for (auto imageViewProxy : computePassDesc.inoutStorageImageProxies)
          {
            if (ImageViewContainsSubresource(GetResolvedImageView(taskIndex, imageViewProxy), imageData, mipLevel, arrayLayer))
              return ImageUsageTypes::ComputeShaderReadWrite;
          }
        }break;
        case Task::Types::ImageTransfer:
        {
          auto &imageTransferDesc = imageTransferDescs[task.index];
          if (ImageViewContainsSubresource(GetResolvedImageView(taskIndex, imageTransferDesc.dstImageViewId), imageData, mipLevel, arrayLayer))
            return ImageUsageTypes::TransferDst;
        }break;
        case Task::Types::ImagePresent:
        {
          auto &imagePresentDesc = imagePresentDescs[task.index];
          if (ImageViewContainsSubresource(GetResolvedImageView(taskIndex, imagePresentDesc.presentImageViewProxyId), imageData, mipLevel, arrayLayer))
            return ImageUsageTypes::Present;
        }break;
      }
      return ImageUsageTypes::None;
    }

    
    BufferUsageTypes GetTaskBufferUsageType(size_t taskIndex, legit::Buffer *buffer)
    {
      Task &task = tasks[taskIndex];
      switch (task.type)
      {
        case Task::Types::RenderPass:
        {
          auto &renderPassDesc = renderPassDescs[task.index];
          for (auto storageBufferProxy : renderPassDesc.inoutStorageBufferProxies)
          {
            auto storageBuffer = GetResolvedBuffer(taskIndex, storageBufferProxy);
            if(buffer->GetHandle() == storageBuffer->GetHandle())
              return BufferUsageTypes::GraphicsShaderReadWrite;
          }
        }break;
        case Task::Types::ComputePass:
        {
          auto &computePassDesc = computePassDescs[task.index];
          for (auto storageBufferProxy : computePassDesc.inoutStorageBufferProxies)
          {
            auto storageBuffer = GetResolvedBuffer(taskIndex, storageBufferProxy);
            if (buffer->GetHandle() == storageBuffer->GetHandle())
              return BufferUsageTypes::ComputeShaderReadWrite;
          }
        }break;
      }
      return BufferUsageTypes::None;
    }

    ImageUsageTypes GetLastImageSubresourceUsageType(size_t taskIndex, legit::ImageData *imageData, uint32_t mipLevel, uint32_t arrayLayer)
    {
      for (size_t taskOffset = 0; taskOffset < taskIndex; taskOffset++)
      {
        size_t prevTaskIndex = taskIndex - taskOffset - 1;
        auto usageType = GetTaskImageSubresourceUsageType(prevTaskIndex, imageData, mipLevel, arrayLayer);
        if (usageType != ImageUsageTypes::None)
          return usageType;
      }

      for (auto &imageViewProxy : imageViewProxies)
      {
        if (imageViewProxy.type == ImageViewProxy::Types::External && imageViewProxy.externalView->GetImageData() == imageData)
        {
          return imageViewProxy.externalUsageType;
        }
      }
      return ImageUsageTypes::None;
    }

    /*ImageUsageTypes GetNextImageSubresourceUsageType(size_t taskIndex, legit::ImageData *imageData, uint32_t mipLevel, uint32_t arrayLayer)
    {
      for (size_t nextTaskIndex = taskIndex + 1; nextTaskIndex < tasks.size(); nextTaskIndex++)
      {
        auto usageType = GetTaskImageSubresourceUsageType(nextTaskIndex, imageData, mipLevel, arrayLayer);
        if (usageType != ImageUsageTypes::Undefined)
          return usageType;
      }
      for (auto &imageViewProxy : imageViewProxies)
      {
        if (imageViewProxy.type == ImageViewProxy::Types::External && imageViewProxy.externalView->GetImageData() == imageData)
        {
          return imageViewProxy.externalUsageType;
        }
      }
      return ImageUsageTypes::Undefined;
    }*/

    BufferUsageTypes GetLastBufferUsageType(size_t taskIndex, legit::Buffer *buffer)
    {
      for (size_t taskOffset = 1; taskOffset < taskIndex; taskOffset++)
      {
        size_t prevTaskIndex = taskIndex - taskOffset;
        auto usageType = GetTaskBufferUsageType(prevTaskIndex, buffer);
        if (usageType != BufferUsageTypes::None)
          return usageType;
      }
      return BufferUsageTypes::None;
    }

    /*BufferUsageTypes GetNextBufferUsageType(size_t taskIndex, legit::Buffer *buffer)
    {
      for (size_t nextTaskIndex = taskIndex + 1; nextTaskIndex < tasks.size(); nextTaskIndex++)
      {
        auto usageType = GetTaskBufferUsageType(nextTaskIndex, buffer);
        if (usageType != BufferUsageTypes::Undefined)
          return usageType;
      }
      return BufferUsageTypes::Undefined;
    }*/

    void FlushImageTransitionBarriers(legit::ImageData *imageData, vk::ImageSubresourceRange range, ImageUsageTypes srcUsageType, ImageUsageTypes dstUsageType, vk::PipelineStageFlags &srcStage, vk::PipelineStageFlags &dstStage, std::vector<vk::ImageMemoryBarrier> &imageBarriers)
    {
      if (
        IsImageBarrierNeeded(srcUsageType, dstUsageType) &&
        //srcUsageType != ImageUsageTypes::ColorAttachment && //this is done automatically when constructing render pass
        //srcUsageType != ImageUsageTypes::DepthAttachment &&
        range.layerCount > 0 &&
        range.levelCount > 0) //this is done automatically when constructing render pass
      {
        auto srcImageAccessPattern = GetSrcImageAccessPattern(srcUsageType);
        auto dstImageAccessPattern = GetDstImageAccessPattern(dstUsageType);
        auto imageBarrier = vk::ImageMemoryBarrier()
          .setSrcAccessMask(srcImageAccessPattern.accessMask)
          .setOldLayout(srcImageAccessPattern.layout)
          .setDstAccessMask(dstImageAccessPattern.accessMask)
          .setNewLayout(dstImageAccessPattern.layout)
          .setSubresourceRange(range)
          .setImage(imageData->GetHandle());

        if (srcImageAccessPattern.queueFamilyType == dstImageAccessPattern.queueFamilyType)
        {
          imageBarrier
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED);
        }
        else
        {
          imageBarrier
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED);
          /*imageBarrier
            .setSrcQueueFamilyIndex(srcImageAccessPattern.queueFamilyIndex)
            .setDstQueueFamilyIndex(dstImageAccessPattern.queueFamilyIndex);*/
        }
        srcStage |= srcImageAccessPattern.stage;
        dstStage |= dstImageAccessPattern.stage;
        imageBarriers.push_back(imageBarrier);
      }
    }

    void AddImageTransitionBarriers(legit::ImageView *imageView, ImageUsageTypes dstUsageType, size_t dstTaskIndex, vk::PipelineStageFlags &srcStage, vk::PipelineStageFlags &dstStage, std::vector<vk::ImageMemoryBarrier> &imageBarriers)
    {
      auto range = vk::ImageSubresourceRange()
        .setAspectMask(imageView->GetImageData()->GetAspectFlags());

      for (uint32_t arrayLayer = imageView->GetBaseArrayLayer(); arrayLayer < imageView->GetBaseArrayLayer() + imageView->GetArrayLayersCount(); arrayLayer++)
      {
        range
          .setBaseArrayLayer(arrayLayer)
          .setLayerCount(1)
          .setBaseMipLevel(imageView->GetBaseMipLevel())
          .setLevelCount(0);
        ImageUsageTypes prevSubresourceUsageType = ImageUsageTypes::None;

        for (uint32_t mipLevel = imageView->GetBaseMipLevel(); mipLevel < imageView->GetBaseMipLevel() + imageView->GetMipLevelsCount(); mipLevel++)
        {
          auto lastUsageType = GetLastImageSubresourceUsageType(dstTaskIndex, imageView->GetImageData(), mipLevel, arrayLayer);
          if (prevSubresourceUsageType != lastUsageType)
          {
            FlushImageTransitionBarriers(imageView->GetImageData(), range, prevSubresourceUsageType, dstUsageType, srcStage, dstStage, imageBarriers);
            range
              .setBaseMipLevel(mipLevel)
              .setLevelCount(0);
            prevSubresourceUsageType = lastUsageType;
          }
          range.levelCount++;
        }
        FlushImageTransitionBarriers(imageView->GetImageData(), range, prevSubresourceUsageType, dstUsageType, srcStage, dstStage, imageBarriers);
      }
    }


    void FlushBufferTransitionBarriers(legit::Buffer *buffer, BufferUsageTypes srcUsageType, BufferUsageTypes dstUsageType, vk::PipelineStageFlags &srcStage, vk::PipelineStageFlags &dstStage, std::vector<vk::BufferMemoryBarrier> &bufferBarriers)
    {
      if (IsBufferBarrierNeeded(srcUsageType, dstUsageType))
      {
        auto srcBufferAccessPattern = GetSrcBufferAccessPattern(srcUsageType);
        auto dstBufferAccessPattern = GetDstBufferAccessPattern(dstUsageType);
        auto bufferBarrier = vk::BufferMemoryBarrier()
          .setSrcAccessMask(srcBufferAccessPattern.accessMask)
          .setOffset(0)
          .setSize(VK_WHOLE_SIZE)
          .setDstAccessMask(dstBufferAccessPattern.accessMask)
          .setBuffer(buffer->GetHandle());

        if (srcBufferAccessPattern.queueFamilyType == dstBufferAccessPattern.queueFamilyType)
        {
          bufferBarrier
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED);
        }
        else
        {
          bufferBarrier
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED);
          /*imageBarrier
          .setSrcQueueFamilyIndex(srcImageAccessPattern.queueFamilyIndex)
          .setDstQueueFamilyIndex(dstImageAccessPattern.queueFamilyIndex);*/
        }
        srcStage |= srcBufferAccessPattern.stage;
        dstStage |= dstBufferAccessPattern.stage;
        bufferBarriers.push_back(bufferBarrier);
      }
    }

    void AddBufferBarriers(legit::Buffer *buffer, BufferUsageTypes dstUsageType, size_t dstTaskIndex, vk::PipelineStageFlags &srcStage, vk::PipelineStageFlags &dstStage, std::vector<vk::BufferMemoryBarrier> &bufferBarriers)
    {
      auto lastUsageType = GetLastBufferUsageType(dstTaskIndex, buffer);
      FlushBufferTransitionBarriers(buffer, lastUsageType, dstUsageType, srcStage, dstStage, bufferBarriers);
    }

    struct ImageProxy
    {
      enum struct Types
      {
        Transient,
        External
      };

      ImageCache::ImageKey imageKey;
      legit::ImageData *externalImage;

      legit::ImageData *resolvedImage;

      Types type;
    };


    ImageCache imageCache;

    ImageProxyPool imageProxies;
    void ResolveImages()
    {
      imageCache.Release();

      for (auto &imageProxy : imageProxies)
      {
        switch (imageProxy.type)
        {
          case ImageProxy::Types::External:
          {
            imageProxy.resolvedImage = imageProxy.externalImage;
          }break;
          case ImageProxy::Types::Transient:
          {
            imageProxy.resolvedImage = imageCache.GetImage(imageProxy.imageKey);
          }break;
        }
      }
    }
    legit::ImageData *GetResolvedImage(size_t taskIndex, ImageProxyId imageProxy)
    {
      return imageProxies.Get(imageProxy).resolvedImage;
    }

    struct ImageViewProxy
    {
      enum struct Types
      {
        Transient,
        External
      };
      bool Contains(const ImageViewProxy &other)
      {
        if (type == Types::Transient && subresourceRange.Contains(other.subresourceRange) && type == other.type && imageProxyId == other.imageProxyId)
        {
          return true;
        }

        if (type == Types::External && externalView == other.externalView)
        {
          return true;
        }
        return false;
      }
      ImageProxyId imageProxyId;
      ImageSubresourceRange subresourceRange;

      legit::ImageView *externalView;
      legit::ImageUsageTypes externalUsageType;

      legit::ImageView *resolvedImageView;

      Types type;
    };

    ImageViewCache imageViewCache;
    ImageViewProxyPool imageViewProxies;
    void ResolveImageViews()
    {
      for (auto &imageViewProxy : imageViewProxies)
      {
        switch (imageViewProxy.type)
        {
          case ImageViewProxy::Types::External:
          {
            imageViewProxy.resolvedImageView = imageViewProxy.externalView;
          }break;
          case ImageViewProxy::Types::Transient:
          {
            ImageViewCache::ImageViewKey imageViewKey;
            imageViewKey.image = GetResolvedImage(0, imageViewProxy.imageProxyId);
            imageViewKey.subresourceRange = imageViewProxy.subresourceRange;

            imageViewProxy.resolvedImageView = imageViewCache.GetImageView(imageViewKey);
          }break;
        }
      }
    }
    legit::ImageView *GetResolvedImageView(size_t taskIndex, ImageViewProxyId imageViewProxyId)
    {
      return imageViewProxies.Get(imageViewProxyId).resolvedImageView;
    }

    struct BufferProxy
    {
      enum struct Types
      {
        Transient,
        External
      };

      BufferCache::BufferKey bufferKey;
      legit::Buffer *externalBuffer;

      legit::Buffer *resolvedBuffer;

      Types type;
    };

    BufferCache bufferCache;
    BufferProxyPool bufferProxies;
    void ResolveBuffers()
    {
      bufferCache.Release();

      for (auto &bufferProxy : bufferProxies)
      {
        switch (bufferProxy.type)
        {
          case BufferProxy::Types::External:
          {
            bufferProxy.resolvedBuffer = bufferProxy.externalBuffer;
          }break;
          case BufferProxy::Types::Transient:
          {
            bufferProxy.resolvedBuffer = bufferCache.GetBuffer(bufferProxy.bufferKey);
          }break;
        }
      }
    }
    legit::Buffer *GetResolvedBuffer(size_t taskIndex, BufferProxyId bufferProxyId)
    {
      return bufferProxies.Get(bufferProxyId).resolvedBuffer;
    }



    std::vector<RenderPassDesc> renderPassDescs;
    std::vector<ComputePassDesc> computePassDescs;

    struct ImageTransferDesc
    {
      ImageViewProxyId dstImageViewId;
      legit::Buffer *srcBuffer;
    };
    std::vector<ImageTransferDesc> imageTransferDescs;


    std::vector<ImagePresentPassDesc> imagePresentDescs;
    std::vector<FrameSyncPassDesc> frameSyncDescs;

    struct Task
    {
      enum struct Types
      {
        RenderPass,
        ComputePass,
        ImageTransfer,
        ImagePresent,
        FrameSync
      };
      Types type;
      size_t index;
    };
    std::vector<Task> tasks;
    void AddTask(Task task)
    {
      tasks.push_back(task);
    }

    legit::ProfilerTask CreateProfilerTask(const RenderPassDesc &renderPassDesc)
    {
      legit::ProfilerTask task;
      task.startTime = -1.0f;
      task.endTime = -1.0f;
      task.name = renderPassDesc.profilerTaskName;
      task.color = renderPassDesc.profilerTaskColor;
      return task;
    }
    legit::ProfilerTask CreateProfilerTask(const ComputePassDesc &computePassDesc)
    {
      legit::ProfilerTask task;
      task.startTime = -1.0f;
      task.endTime = -1.0f;
      task.name = computePassDesc.profilerTaskName;
      task.color = computePassDesc.profilerTaskColor;
      return task;
    }
    legit::ProfilerTask CreateProfilerTask(const ImagePresentPassDesc &imagePresentPassDesc)
    {
      legit::ProfilerTask task;
      task.startTime = -1.0f;
      task.endTime = -1.0f;
      task.name = "ImagePresent";
      task.color = glm::packUnorm4x8(glm::vec4(0.0f, 1.0f, 0.5f, 1.0f));
      return task;
    }

    legit::ProfilerTask CreateProfilerTask(const FrameSyncPassDesc &frameSyncPassDesc)
    {
      legit::ProfilerTask task;
      task.startTime = -1.0f;
      task.endTime = -1.0f;
      task.name = "FrameSync";
      task.color = glm::packUnorm4x8(glm::vec4(0.0f, 0.5f, 1.0f, 1.0f));
      return task;
    }

    RenderPassCache renderPassCache;
    FramebufferCache framebufferCache;

    vk::Device logicalDevice;
    vk::PhysicalDevice physicalDevice;
    size_t imageAllocations = 0;
  };

}