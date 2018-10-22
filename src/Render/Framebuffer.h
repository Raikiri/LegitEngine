namespace legit
{
  class Core;
  class Framebuffer
  {
  public:
    vk::Framebuffer GetHandle()
    {
      return framebuffer.get();
    }
  private:
    Framebuffer(vk::Device logicalDevice, const std::vector<const ImageView*> &imageViews, vk::Extent2D size, vk::RenderPass renderPass)
    {
      std::vector<vk::ImageView> imageViewHandles;
      for (auto imageView : imageViews)
        imageViewHandles.push_back(imageView->GetImageViewHandle());

      auto framebufferInfo = vk::FramebufferCreateInfo()
        .setAttachmentCount(uint32_t(imageViewHandles.size()))
        .setPAttachments(imageViewHandles.data())
        .setRenderPass(renderPass)
        .setWidth(size.width)
        .setHeight(size.height)
        .setLayers(1);

      this->framebuffer = logicalDevice.createFramebufferUnique(framebufferInfo);
    }
    vk::UniqueFramebuffer framebuffer;
    friend class Core;
  };
}