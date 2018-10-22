namespace legit
{
  class Swapchain;
  class Core;
  class ImageView
  {
  public:
    vk::ImageView GetImageViewHandle() const
    {
      return imageView.get();
    }
    vk::Format GetFormat() const
    {
      return format;
    }
  private:
    ImageView(vk::Device logicalDevice, vk::Image image, vk::ImageViewType viewType, vk::Format format, uint32_t baseMipLevel, uint32_t mipsCount)
    {
      this->image = image;
      this->format = format;
      auto subresourceRange = vk::ImageSubresourceRange()
        .setAspectMask(vk::ImageAspectFlagBits::eColor)
        .setBaseMipLevel(baseMipLevel)
        .setLevelCount(mipsCount)
        .setBaseArrayLayer(0)
        .setLayerCount(1);
        
      auto imageViewCreateInfo = vk::ImageViewCreateInfo()
        .setImage(image)
        .setViewType(viewType)
        .setFormat(format)
        .setSubresourceRange(subresourceRange);
      imageView = logicalDevice.createImageViewUnique(imageViewCreateInfo);
    }
    vk::UniqueImageView imageView;
    vk::Image image;
    vk::Format format;

    friend class Swapchain;
    friend class Core;
  };
}