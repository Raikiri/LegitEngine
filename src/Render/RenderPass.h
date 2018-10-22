namespace legit
{
  class Core;
  class RenderPass
  {
  public:
    vk::RenderPass GetHandle()
    {
      return renderPass.get();
    }
  private:
    RenderPass(vk::Device logicalDevice, std::vector<vk::Format> attachmentFormats)
    {
      std::vector<vk::AttachmentDescription> attachmentDescs;

      for (auto attachmentFormat : attachmentFormats)
      {
        auto attachmentDesc = vk::AttachmentDescription()
          .setFormat(attachmentFormat)
          .setSamples(vk::SampleCountFlagBits::e1)
          .setLoadOp(vk::AttachmentLoadOp::eClear)
          .setStoreOp(vk::AttachmentStoreOp::eStore)
          .setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
          .setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
          .setInitialLayout(vk::ImageLayout::eUndefined)
          .setFinalLayout(vk::ImageLayout::ePresentSrcKHR);
        attachmentDescs.push_back(attachmentDesc);
      }

      std::vector <vk::AttachmentReference> attachmentRefs;

      auto subpassAttachmentRef = vk::AttachmentReference()
        .setAttachment(0)
        .setLayout(vk::ImageLayout::eColorAttachmentOptimal);

      auto subpass = vk::SubpassDescription()
        .setPipelineBindPoint(vk::PipelineBindPoint::eGraphics)
        .setColorAttachmentCount(1)
        .setPColorAttachments(&subpassAttachmentRef);

      auto subpassDependency = vk::SubpassDependency()
        .setSrcSubpass(VK_SUBPASS_EXTERNAL)
        .setSrcStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput)
        .setSrcAccessMask(vk::AccessFlags())
        .setDstSubpass(0)
        .setDstStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput)
        .setDstAccessMask(vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite);

      auto renderPassInfo = vk::RenderPassCreateInfo()
        .setAttachmentCount(uint32_t(attachmentDescs.size()))
        .setPAttachments(attachmentDescs.data())
        .setSubpassCount(1)
        .setPSubpasses(&subpass)
        .setDependencyCount(1)
        .setPDependencies(&subpassDependency);

      this->renderPass = logicalDevice.createRenderPassUnique(renderPassInfo);
    }
    vk::UniqueRenderPass renderPass;
    friend class Core;
  };
}