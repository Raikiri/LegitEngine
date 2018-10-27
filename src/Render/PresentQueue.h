namespace legit
{
  struct PresentQueue
  {
    PresentQueue(legit::Core *core, legit::WindowDesc windowDesc)
    {
      this->core = core;
      swapchain = core->CreateSwapchain(windowDesc);
      auto swapchainImageViews = swapchain->GetImageViews();
      swapchainImages.resize(swapchainImageViews.size());
      for (size_t imageIndex = 0; imageIndex < swapchainImageViews.size(); imageIndex++)
      {
        auto &image = swapchainImages[imageIndex];
        image.imageView = swapchainImageViews[imageIndex];
        image.inFlightFence = core->CreateFence(true);
        image.commandBuffer = std::move(core->AllocateCommandBuffers(1)[0]);
      }
      imageAcquiredSemaphore = core->CreateVulkanSemaphore();
      renderingFinishedSemaphore = core->CreateVulkanSemaphore();

      swapchainRect = vk::Rect2D(vk::Offset2D(), swapchain->GetSize());

      imageIndex = 0;
    }
    struct FrameInfo
    {
      const legit::ImageView *imageView;
      vk::CommandBuffer commandBuffer;
    };
    FrameInfo BeginFrame()
    {
      passIndex = 0;
      imageIndex = swapchain->AcquireNextImage(imageAcquiredSemaphore.get()).value;
      auto &currFrame = swapchainImages[imageIndex];
      core->WaitForFence(currFrame.inFlightFence.get());
      core->ResetFence(currFrame.inFlightFence.get());

      auto bufferBeginInfo = vk::CommandBufferBeginInfo()
        .setFlags(vk::CommandBufferUsageFlagBits::eSimultaneousUse);
      currFrame.commandBuffer->begin(bufferBeginInfo);

      FrameInfo frameInfo;
      frameInfo.imageView = swapchain->GetImageViews()[imageIndex];
      frameInfo.commandBuffer = currFrame.commandBuffer.get();
      return frameInfo;
    }
    void EndFrame()
    {
      auto &currFrame = swapchainImages[imageIndex];
      currFrame.commandBuffer->end();

      {
        vk::Semaphore waitSemaphores[] = { imageAcquiredSemaphore.get() };
        vk::Semaphore signalSemaphores[] = { renderingFinishedSemaphore.get() };
        vk::PipelineStageFlags waitStages[] = { vk::PipelineStageFlagBits::eColorAttachmentOutput };

        auto submitInfo = vk::SubmitInfo()
          .setWaitSemaphoreCount(1)
          .setPWaitSemaphores(waitSemaphores)
          .setPWaitDstStageMask(waitStages)
          .setCommandBufferCount(1)
          .setPCommandBuffers(&currFrame.commandBuffer.get())
          .setSignalSemaphoreCount(1)
          .setPSignalSemaphores(signalSemaphores);

        core->GetGraphicsQueue().submit({ submitInfo }, currFrame.inFlightFence.get());
      }

      {
        vk::SwapchainKHR swapchains[] = { swapchain->GetHandle() };
        vk::Semaphore waitSemaphores[] = { renderingFinishedSemaphore.get() };
        auto presentInfo = vk::PresentInfoKHR()
          .setSwapchainCount(1)
          .setPSwapchains(swapchains)
          .setPImageIndices(&imageIndex)
          .setPResults(nullptr)
          .setWaitSemaphoreCount(1)
          .setPWaitSemaphores(waitSemaphores);

        core->GetPresentQueue().presentKHR(presentInfo);
      }
    }
    vk::Extent2D GetSize()
    {
      return swapchain->GetSize();
    }
  private:
    legit::Core *core;
    std::unique_ptr<legit::Swapchain> swapchain;

    struct SwapchainImage
    {
      const legit::ImageView *imageView;
      vk::UniqueCommandBuffer commandBuffer;
      vk::UniqueFence inFlightFence;
    };

    std::vector<SwapchainImage> swapchainImages;
    uint32_t imageIndex;
    size_t maxPassesCount;
    vk::UniqueSemaphore imageAcquiredSemaphore;
    vk::UniqueSemaphore renderingFinishedSemaphore;
    size_t passIndex;

    vk::Rect2D swapchainRect;
  };
}