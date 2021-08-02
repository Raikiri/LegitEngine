namespace legit
{
  class Framebuffer
  {
  public:
    vk::Framebuffer GetHandle()
    {
      return framebuffer.get();
    }
    Framebuffer(vk::Device logicalDevice, const std::vector<const ImageView*> &imageViews, vk::Extent2D size, vk::RenderPass renderPass)
    {
      std::vector<vk::ImageView> imageViewHandles;
      for (auto imageView : imageViews)
        imageViewHandles.push_back(imageView->GetHandle());

      auto framebufferInfo = vk::FramebufferCreateInfo()
        .setAttachmentCount(uint32_t(imageViewHandles.size()))
        .setPAttachments(imageViewHandles.data())
        .setRenderPass(renderPass)
        .setWidth(size.width)
        .setHeight(size.height)
        .setLayers(1);

      this->framebuffer = logicalDevice.createFramebufferUnique(framebufferInfo);
    }
  private:
    vk::UniqueFramebuffer framebuffer;
  };
}