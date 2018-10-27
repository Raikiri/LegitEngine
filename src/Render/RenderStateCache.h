#include <map>
namespace legit
{
  class RenderStateCache
  {
  public:
    void BeginPass(
      vk::CommandBuffer commandBuffer,
      const std::vector<const legit::ImageView *> colorAttachments, const legit::ImageView *depthAttachment,
      legit::VertexDeclaration vertexDeclaration,
      ShaderModule *vertexShader,
      ShaderModule *fragmentShader,
      vk::Extent2D renderAreaExtent)
    {
      RenderPassKey renderPassKey;
      FramebufferKey framebufferKey;

      renderPassKey.passType = true;
      size_t attachmentsUsed = 0;
      for (auto attachment : colorAttachments)
      {
        renderPassKey.colorFormats[attachmentsUsed] = attachment->GetFormat();
        framebufferKey.colorAttachments[attachmentsUsed] = attachment;
      }
      renderPassKey.depthFormat = depthAttachment ? depthAttachment->GetFormat() : vk::Format::eUndefined;
      framebufferKey.depthAttachment = depthAttachment;

      legit::RenderPass *renderPass = GetRenderPass(renderPassKey);

      framebufferKey.extent = renderAreaExtent;
      framebufferKey.renderPass = renderPass->GetHandle();
      legit::Framebuffer *framebuffer = GetFramebuffer(framebufferKey);

      PipelineKey pipelineKey;
      pipelineKey.vertexShader = vertexShader->GetHandle();
      pipelineKey.fragmentShader = fragmentShader->GetHandle();
      pipelineKey.vertexDecl = vertexDeclaration;
      pipelineKey.renderPass = renderPass->GetHandle();

      Pipeline *pipeline = GetPipeline(pipelineKey);

      auto clearValue = vk::ClearValue().
        setColor(vk::ClearColorValue().setFloat32({ 0.0f, 0.0f, 0.0f, 1.0f }));

      vk::Rect2D rect = vk::Rect2D(vk::Offset2D(), renderAreaExtent);
      auto passBeginInfo = vk::RenderPassBeginInfo()
        .setRenderPass(renderPass->GetHandle())
        .setFramebuffer(framebuffer->GetHandle())
        .setRenderArea(rect)
        .setClearValueCount(1)
        .setPClearValues(&clearValue);
      commandBuffer.beginRenderPass(passBeginInfo, vk::SubpassContents::eInline);

      commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline->GetHandle());

      auto viewport = vk::Viewport()
        .setWidth(float(renderAreaExtent.width))
        .setHeight(float(renderAreaExtent.height))
        .setMinDepth(0.0f)
        .setMaxDepth(1.0f);

      commandBuffer.setViewport(0, { viewport });
      commandBuffer.setScissor(0, { vk::Rect2D(vk::Offset2D(), renderAreaExtent) });
    }
    void EndPass(vk::CommandBuffer commandBuffer)
    {
      commandBuffer.endRenderPass();
    }
    RenderStateCache(Core *core)
    {
      this->core = core;
    }
  private:

    struct RenderPassKey
    {
      RenderPassKey()
      {
        std::fill(colorFormats.begin(), colorFormats.end(), vk::Format::eUndefined);
        depthFormat = vk::Format::eUndefined;
        passType = false;
      }
      std::array<vk::Format, 8> colorFormats;
      vk::Format depthFormat;
      bool passType;
      bool operator < (const RenderPassKey &other) const
      {
        return std::tie(colorFormats, depthFormat, passType) < std::tie(other.colorFormats, other.depthFormat, other.passType);
      }
    };
    struct FramebufferKey
    {
      FramebufferKey()
      {
        std::fill(colorAttachments.begin(), colorAttachments.end(), nullptr);
        depthAttachment = nullptr;
        renderPass = nullptr;
      }
      std::array<const legit::ImageView *, 8> colorAttachments;
      const legit::ImageView *depthAttachment;
      vk::Extent2D extent;
      vk::RenderPass renderPass;
      bool operator < (const FramebufferKey  &other) const
      {
        return std::tie(colorAttachments, depthAttachment, extent.width, extent.height) < std::tie(other.colorAttachments, other.depthAttachment, other.extent.width, other.extent.height);
      }
    };
    struct PipelineKey
    {
      PipelineKey()
      {
        vertexShader = nullptr;
        fragmentShader = nullptr;
        renderPass = nullptr;
      }
      vk::ShaderModule vertexShader;
      vk::ShaderModule fragmentShader;
      legit::VertexDeclaration vertexDecl;
      vk::Extent2D extent;
      vk::RenderPass renderPass;
      bool operator < (const PipelineKey  &other) const
      {
        return 
          std::tie(      vertexShader,       fragmentShader,       vertexDecl,       renderPass) < 
          std::tie(other.vertexShader, other.fragmentShader, other.vertexDecl, other.renderPass);
      }
    };
    legit::RenderPass *GetRenderPass(const RenderPassKey &key)
    {
      auto &renderPass = renderPassCache[key];
      if (!renderPass)
      {
        std::vector<vk::Format> formats;
        for (auto format : key.colorFormats)
        {
          if (format != vk::Format::eUndefined)
            formats.push_back(format);
        }
        renderPass = core->CreateRenderPass(formats);
      }
      return renderPass.get();
    }
    legit::Pipeline *GetPipeline(const PipelineKey &key)
    {
      auto &pipeline = pipelineCache[key];
      if (!pipeline)
        pipeline = core->CreatePipeline(key.vertexShader, key.fragmentShader, key.vertexDecl, key.renderPass);
      return pipeline.get();
    }
    legit::Framebuffer *GetFramebuffer(FramebufferKey key)
    {
      auto &framebuffer = framebufferCache[key];

      if (!framebuffer)
      {
        std::vector<const legit::ImageView *> imageViews;
        for (auto imageView : key.colorAttachments)
        {
          if (imageView)
            imageViews.push_back(imageView);
        }
        if (key.depthAttachment)
          imageViews.push_back(key.depthAttachment);

        framebuffer = core->CreateFramebuffer(imageViews, key.extent, key.renderPass);
      }
      else
      {
        int p = 1;
      }
      return framebuffer.get();
    }
    std::map<RenderPassKey, std::unique_ptr<legit::RenderPass>> renderPassCache;
    std::map<PipelineKey, std::unique_ptr<legit::Pipeline>> pipelineCache;
    std::map<FramebufferKey, std::unique_ptr<legit::Framebuffer>> framebufferCache;
    legit::Core *core;
    friend class Core;
  };
}