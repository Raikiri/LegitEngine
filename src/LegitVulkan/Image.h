namespace legit
{
  static bool IsDepthFormat(vk::Format format)
  {
    return (format >= vk::Format::eD16Unorm && format < vk::Format::eD32SfloatS8Uint);
  }

  static vk::ImageUsageFlags GetGeneralUsageFlags(vk::Format format)
  {
    vk::ImageUsageFlags usageFlags = vk::ImageUsageFlagBits::eSampled;
    if (IsDepthFormat(format))
    {
      usageFlags |= vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled;
    }
    else
    {
      usageFlags |= vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
    }
    return usageFlags;
  }
  vk::ImageUsageFlags colorImageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
  vk::ImageUsageFlags depthImageUsage = vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled;

  class Swapchain;
  class RenderTarget;
  class Image;

  struct ImageSubresourceRange
  {
    bool Contains(const ImageSubresourceRange &other)
    {
      return baseMipLevel <= other.baseMipLevel && baseArrayLayer <= other.baseArrayLayer && mipsCount >= other.mipsCount && arrayLayersCount >= other.arrayLayersCount;
    }

    bool operator < (const ImageSubresourceRange &other) const
    {
      return std::tie(baseMipLevel, mipsCount, baseArrayLayer, arrayLayersCount) < std::tie(other.baseMipLevel, other.mipsCount, other.baseArrayLayer, other.arrayLayersCount);
    }
    uint32_t baseMipLevel;
    uint32_t mipsCount;
    uint32_t baseArrayLayer;
    uint32_t arrayLayersCount;
  };

  class ImageData
  {
  public:
    vk::Image GetHandle() const
    {
      return imageHandle;
    }
    vk::Format GetFormat() const
    {
      return format;
    }
    vk::ImageType GetType() const
    {
      return imageType;
    }
    glm::uvec3 GetMipSize(uint32_t mipLevel)
    {
      return mipInfos[mipLevel].size;
    }
    vk::ImageAspectFlags GetAspectFlags() const
    {
      return aspectFlags;
    }
    uint32_t GetArrayLayersCount()
    {
      return arrayLayersCount;
    }
    uint32_t GetMipsCount()
    {
      return mipsCount;
    }
    bool operator <(const ImageData &other) const
    {
      return std::tie(imageHandle) < std::tie(other.imageHandle);
    }

  private:
    ImageData(vk::Image imageHandle, vk::ImageType imageType, glm::uvec3 size, uint32_t mipsCount, uint32_t arrayLayersCount, vk::Format format, vk::ImageLayout layout)
    {
      this->imageHandle = imageHandle;
      this->format = format;
      this->mipsCount = mipsCount;
      this->arrayLayersCount = arrayLayersCount;
      this->imageType = imageType;

      glm::vec3 currSize = size;

      for (size_t mipLevel = 0; mipLevel < mipsCount; mipLevel++)
      {
        MipInfo mipInfo;
        mipInfo.size = currSize;
        currSize.x /= 2;
        if (imageType == vk::ImageType::e2D || imageType == vk::ImageType::e3D)
          currSize.y /= 2;
        if (imageType == vk::ImageType::e3D)
          currSize.z /= 2;

        mipInfo.layerInfos.resize(arrayLayersCount);
        for (size_t layerIndex = 0; layerIndex < arrayLayersCount; layerIndex++)
        {
          mipInfo.layerInfos[layerIndex].currLayout = layout;
        }
        mipInfos.push_back(mipInfo);
      }

      if (legit::IsDepthFormat(format))
        this->aspectFlags = vk::ImageAspectFlagBits::eDepth;// | vk::ImageAspectFlagBits::eStencil;
      else
        this->aspectFlags = vk::ImageAspectFlagBits::eColor;
    }

    void SetDebugName(std::string _debugName)
    {
      this->debugName = _debugName;
    }

    struct SubImageInfo
    {
      vk::ImageLayout currLayout;
    };
    struct MipInfo
    {
      std::vector<SubImageInfo> layerInfos;
      glm::uvec3 size;
    };
    std::vector<MipInfo> mipInfos;

    vk::ImageAspectFlags aspectFlags;
    vk::Image imageHandle;
    vk::Format format;
    vk::ImageType imageType;
    uint32_t mipsCount;
    uint32_t arrayLayersCount;
    std::string debugName;

    friend class legit::Image;
    friend class legit::Swapchain;
    friend class Core;
  };

  class Image
  {
  public:
    legit::ImageData *GetImageData()
    {
      return imageData.get();
    }
    vk::DeviceMemory GetMemory()
    {
      return imageMemory.get();
    }

    static vk::ImageCreateInfo CreateInfo2d(glm::uvec2 size, uint32_t mipsCount, uint32_t arrayLayersCount, vk::Format format, vk::ImageUsageFlags usage)
    {
      auto layout = vk::ImageLayout::eUndefined;
      auto imageInfo = vk::ImageCreateInfo()
        .setImageType(vk::ImageType::e2D)
        .setExtent(vk::Extent3D(size.x, size.y, 1))
        .setMipLevels(mipsCount)
        .setArrayLayers(arrayLayersCount)
        .setFormat(format)
        .setInitialLayout(layout) //images must be created in undefined layout
        .setUsage(usage)
        .setSharingMode(vk::SharingMode::eExclusive)
        .setSamples(vk::SampleCountFlagBits::e1)
        .setFlags(vk::ImageCreateFlags());/*
                                          .setTiling(vk::ImageTiling::eOptimal);*/
      return imageInfo;
    }

    static vk::ImageCreateInfo CreateInfoVolume(glm::uvec3 size, uint32_t mipsCount, uint32_t arrayLayersCount, vk::Format format, vk::ImageUsageFlags usage)
    {
      auto layout = vk::ImageLayout::eUndefined;
      auto imageInfo = vk::ImageCreateInfo()
        .setImageType(vk::ImageType::e3D)
        .setExtent(vk::Extent3D(size.x, size.y, size.z))
        .setMipLevels(mipsCount)
        .setArrayLayers(arrayLayersCount)
        .setFormat(format)
        .setInitialLayout(layout) //images must be created in undefined layout
        .setUsage(usage)
        .setSharingMode(vk::SharingMode::eExclusive)
        .setSamples(vk::SampleCountFlagBits::e1)
        .setFlags(vk::ImageCreateFlags());/*
                                          .setTiling(vk::ImageTiling::eOptimal);*/
      return imageInfo;
    }

    static vk::ImageCreateInfo CreateInfoCube(glm::uvec2 size, uint32_t mipsCount, vk::Format format, vk::ImageUsageFlags usage)
    {
      auto layout = vk::ImageLayout::eUndefined;
      auto imageInfo = vk::ImageCreateInfo()
        .setImageType(vk::ImageType::e2D)
        .setExtent(vk::Extent3D(size.x, size.y, 1))
        .setMipLevels(mipsCount)
        .setArrayLayers(6)
        .setFormat(format)
        .setInitialLayout(layout) //images must be created in undefined layout
        .setUsage(usage)
        .setSharingMode(vk::SharingMode::eExclusive)
        .setSamples(vk::SampleCountFlagBits::e1)
        .setFlags(vk::ImageCreateFlagBits::eCubeCompatible);/*
                                          .setTiling(vk::ImageTiling::eOptimal);*/
      return imageInfo;
    }


    Image(vk::PhysicalDevice physicalDevice, vk::Device logicalDevice, vk::ImageCreateInfo imageInfo)
    {
      imageHandle = logicalDevice.createImageUnique(imageInfo);
      glm::uvec3 size = { imageInfo.extent.width, imageInfo.extent.height, imageInfo.extent.depth };

      imageData.reset(new legit::ImageData(imageHandle.get(), imageInfo.imageType, size, imageInfo.mipLevels, imageInfo.arrayLayers, imageInfo.format, imageInfo.initialLayout));

      vk::MemoryRequirements imageMemRequirements = logicalDevice.getImageMemoryRequirements(imageHandle.get());

      auto allocInfo = vk::MemoryAllocateInfo()
        .setAllocationSize(imageMemRequirements.size)
        .setMemoryTypeIndex(legit::FindMemoryTypeIndex(physicalDevice, imageMemRequirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal));

      imageMemory = logicalDevice.allocateMemoryUnique(allocInfo);

      logicalDevice.bindImageMemory(imageHandle.get(), imageMemory.get(), 0);
    }
  private:
    vk::UniqueImage imageHandle;
    std::unique_ptr<legit::ImageData> imageData;
    vk::UniqueDeviceMemory imageMemory;
  };
}