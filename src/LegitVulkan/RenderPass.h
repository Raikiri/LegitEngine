#pragma once
namespace vk
{
  bool operator < (const vk::ClearValue &v0, const vk::ClearValue &v1)
  {
    return
      std::tie(v0.color.int32[0], v0.color.int32[1], v0.color.int32[2], v0.color.int32[3]) <
      std::tie(v1.color.int32[0], v1.color.int32[1], v1.color.int32[2], v1.color.int32[3]);
  }
}
namespace legit
{
  class RenderPass
  {
  public:
    vk::RenderPass GetHandle()
    {
      return renderPass.get();
    }
    size_t GetColorAttachmentsCount()
    {
      return colorAttachmentDescs.size();
    }
    struct AttachmentDesc
    {
      vk::Format format;
      vk::AttachmentLoadOp loadOp;
      vk::ClearValue clearValue;
      bool operator <(const AttachmentDesc &other) const
      {
        return 
          std::tie(      format,       loadOp,       clearValue) <
          std::tie(other.format, other.loadOp, other.clearValue);
      }
    };
    RenderPass(vk::Device logicalDevice, std::vector<AttachmentDesc> _colorAttachments, AttachmentDesc _depthAttachment)
    {
      this->colorAttachmentDescs = _colorAttachments;
      this->depthAttachmentDesc = _depthAttachment;

      /*auto subpassDependency = vk::SubpassDependency()
        .setSrcSubpass(VK_SUBPASS_EXTERNAL)
        .setDstSubpass(0);*/

      std::vector <vk::AttachmentReference> colorAttachmentRefs;

      uint32_t currAttachmentIndex = 0;

      std::vector<vk::AttachmentDescription> attachmentDescs;
      for (auto colorAttachmentDesc : colorAttachmentDescs)
      {
        /*auto srcAccessPattern = GetImageAccessPattern(colorAttachmentDesc.srcUsageType, true);
        auto dstAccessPattern = GetImageAccessPattern(colorAttachmentDesc.dstUsageType, false);*/

        colorAttachmentRefs.push_back(vk::AttachmentReference()
          .setAttachment(currAttachmentIndex++)
          .setLayout(vk::ImageLayout::eColorAttachmentOptimal));

        auto attachmentDesc = vk::AttachmentDescription()
          .setFormat(colorAttachmentDesc.format)
          .setSamples(vk::SampleCountFlagBits::e1)
          .setLoadOp(colorAttachmentDesc.loadOp)
          .setStoreOp(vk::AttachmentStoreOp::eStore)
          .setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
          .setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
          /*.setInitialLayout(srcAccessPattern.layout)
          .setFinalLayout(dstAccessPattern.layout)*/
          .setInitialLayout(vk::ImageLayout::eColorAttachmentOptimal)
          .setFinalLayout(vk::ImageLayout::eColorAttachmentOptimal);
        attachmentDescs.push_back(attachmentDesc);

        /*subpassDependency.srcStageMask |= srcAccessPattern.stage;
        subpassDependency.srcAccessMask |= srcAccessPattern.accessMask;
        subpassDependency.dstStageMask |= dstAccessPattern.stage;
        subpassDependency.dstAccessMask |= dstAccessPattern.accessMask;*/

        /*auto subpassDependency1 = vk::SubpassDependency()
          .setSrcSubpass(VK_SUBPASS_EXTERNAL)
          .setSrcStageMask(vk::PipelineStageFlagBits::eComputeShader)
          .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
          .setDstSubpass(0)
          .setDstStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput)
          .setDstAccessMask(vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite);

        auto subpassDependency2 = vk::SubpassDependency()
          .setSrcSubpass(0)
          .setSrcStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput)
          .setSrcAccessMask(vk::AccessFlagBits::eColorAttachmentWrite)
          .setDstSubpass(VK_SUBPASS_EXTERNAL)
          .setDstStageMask(vk::PipelineStageFlagBits::eComputeShader)
          .setDstAccessMask(vk::AccessFlagBits::eShaderRead);*/
      }

      
      auto subpass = vk::SubpassDescription()
        .setPipelineBindPoint(vk::PipelineBindPoint::eGraphics)
        .setColorAttachmentCount(uint32_t(colorAttachmentRefs.size()))
        .setPColorAttachments(colorAttachmentRefs.data());

      vk::AttachmentDescription depthDesc;
      vk::AttachmentReference depthRef;
      if (depthAttachmentDesc.format != vk::Format::eUndefined)
      {
        /*auto srcAccessPattern = GetImageAccessPattern(depthAttachmentDesc.srcUsageType, true);
        auto dstAccessPattern = GetImageAccessPattern(depthAttachmentDesc.dstUsageType, false);*/

        depthRef
          .setAttachment(currAttachmentIndex++)
          .setLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal);

        auto attachmentDesc = vk::AttachmentDescription()
          .setFormat(depthAttachmentDesc.format)
          .setSamples(vk::SampleCountFlagBits::e1)
          .setLoadOp(depthAttachmentDesc.loadOp)
          .setStoreOp(vk::AttachmentStoreOp::eStore)
          .setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
          .setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
          /*.setInitialLayout(srcAccessPattern.layout)
          .setFinalLayout(dstAccessPattern.layout)*/
          .setInitialLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal)
          .setFinalLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal);
        attachmentDescs.push_back(attachmentDesc);

        subpass
          .setPDepthStencilAttachment(&depthRef);

        /*subpassDependency.srcStageMask |= srcAccessPattern.stage;
        subpassDependency.srcAccessMask |= srcAccessPattern.accessMask;
        subpassDependency.dstStageMask |= dstAccessPattern.stage;
        subpassDependency.dstAccessMask |= dstAccessPattern.accessMask;*/
      }

      auto renderPassInfo = vk::RenderPassCreateInfo()
        .setAttachmentCount(uint32_t(attachmentDescs.size()))
        .setPAttachments(attachmentDescs.data())
        .setSubpassCount(1)
        .setPSubpasses(&subpass)
        .setDependencyCount(0)
        .setPDependencies(nullptr);
        //.setDependencyCount(1)
        //.setPDependencies(&subpassDependency);

      this->renderPass = logicalDevice.createRenderPassUnique(renderPassInfo);
    }
  private:
    vk::UniqueRenderPass renderPass;
    std::vector<AttachmentDesc> colorAttachmentDescs;
    AttachmentDesc depthAttachmentDesc;
  };
}