namespace legit
{
  class Swapchain;
  class RenderTarget;

  class ImageView
  {
  public:
    vk::ImageView GetHandle() const
    {
      return imageView.get();
    }
    legit::ImageData *GetImageData()
    {
      return imageData;
    }
    const legit::ImageData *GetImageData() const
    {
      return imageData;
    }
    uint32_t GetBaseMipLevel() { return baseMipLevel; }
    uint32_t GetMipLevelsCount() { return mipLevelsCount; }
    uint32_t GetBaseArrayLayer() { return baseArrayLayer; }
    uint32_t GetArrayLayersCount() { return arrayLayersCount; }
    ImageView(vk::Device logicalDevice, legit::ImageData *imageData, uint32_t baseMipLevel, uint32_t mipLevelsCount, uint32_t baseArrayLayer, uint32_t arrayLayersCount)
    {
      this->imageData = imageData;
      this->baseMipLevel = baseMipLevel;
      this->mipLevelsCount = mipLevelsCount;
      this->baseArrayLayer = baseArrayLayer;
      this->arrayLayersCount = arrayLayersCount;

      vk::Format format = imageData->GetFormat();
      vk::ImageAspectFlags aspectFlags;


      auto subresourceRange = vk::ImageSubresourceRange()
        .setAspectMask(imageData->GetAspectFlags())
        .setBaseMipLevel(baseMipLevel)
        .setLevelCount(mipLevelsCount)
        .setBaseArrayLayer(baseArrayLayer)
        .setLayerCount(arrayLayersCount);


      vk::ImageViewType viewType;
      if (imageData->GetType() == vk::ImageType::e1D)
        viewType = vk::ImageViewType::e1D;
      if (imageData->GetType() == vk::ImageType::e2D)
        viewType = vk::ImageViewType::e2D;
      if (imageData->GetType() == vk::ImageType::e3D)
        viewType = vk::ImageViewType::e3D;


      auto imageViewCreateInfo = vk::ImageViewCreateInfo()
        .setImage(imageData->GetHandle())
        .setViewType(viewType)
        .setFormat(format)
        .setSubresourceRange(subresourceRange);
      imageView = logicalDevice.createImageViewUnique(imageViewCreateInfo);
    }
    ImageView(vk::Device logicalDevice, legit::ImageData *cubemapImageData, uint32_t baseMipLevel, uint32_t mipLevelsCount)
    {
      this->imageData = cubemapImageData;
      this->baseMipLevel = baseMipLevel;
      this->mipLevelsCount = mipLevelsCount;
      this->baseArrayLayer = 0;
      this->arrayLayersCount = 6;

      vk::Format format = imageData->GetFormat();
      vk::ImageAspectFlags aspectFlags;


      auto subresourceRange = vk::ImageSubresourceRange()
        .setAspectMask(imageData->GetAspectFlags())
        .setBaseMipLevel(baseMipLevel)
        .setLevelCount(mipLevelsCount)
        .setBaseArrayLayer(baseArrayLayer)
        .setLayerCount(arrayLayersCount);


      vk::ImageViewType viewType = vk::ImageViewType::eCube;
      assert(imageData->GetType() == vk::ImageType::e2D);

      auto imageViewCreateInfo = vk::ImageViewCreateInfo()
        .setImage(imageData->GetHandle())
        .setViewType(viewType)
        .setFormat(format)
        .setSubresourceRange(subresourceRange);
      imageView = logicalDevice.createImageViewUnique(imageViewCreateInfo);
    }
  private:
    vk::UniqueImageView imageView;
    legit::ImageData *imageData;
    uint32_t baseMipLevel;
    uint32_t mipLevelsCount;

    uint32_t baseArrayLayer;
    uint32_t arrayLayersCount;

    friend class legit::Swapchain;
    friend class legit::RenderTarget;
  };
}