namespace legit
{
  struct PresentQueue
  {
    PresentQueue(legit::Core *core, legit::WindowDesc windowDesc, uint32_t imagesCount, vk::PresentModeKHR preferredMode)
    {
      this->core = core;
      this->swapchain = core->CreateSwapchain(windowDesc, imagesCount, preferredMode);
      this->swapchainImageViews = swapchain->GetImageViews();
      this->swapchainRect = vk::Rect2D(vk::Offset2D(), swapchain->GetSize());
      this->imageIndex = -1;
    }
    legit::ImageView *AcquireImage(vk::Semaphore signalSemaphore)
    {
      this->imageIndex = swapchain->AcquireNextImage(signalSemaphore).value;
      return swapchainImageViews[imageIndex];
    }
    void PresentImage(vk::Semaphore waitSemaphore)
    {
      vk::SwapchainKHR swapchains[] = { swapchain->GetHandle() };
      vk::Semaphore waitSemaphores[] = { waitSemaphore };
      auto presentInfo = vk::PresentInfoKHR()
        .setSwapchainCount(1)
        .setPSwapchains(swapchains)
        .setPImageIndices(&imageIndex)
        .setPResults(nullptr)
        .setWaitSemaphoreCount(1)
        .setPWaitSemaphores(waitSemaphores);

      core->GetPresentQueue().presentKHR(presentInfo);
    }
    vk::Extent2D GetImageSize()
    {
      return swapchain->GetSize();
    }
  private:
    legit::Core *core;
    std::unique_ptr<legit::Swapchain> swapchain;

    std::vector<legit::ImageView *> swapchainImageViews;
    uint32_t imageIndex;

    vk::Rect2D swapchainRect;
  };
  
  struct InFlightQueue
  {
    InFlightQueue(legit::Core *core, legit::WindowDesc windowDesc, uint32_t inFlightCount, vk::PresentModeKHR preferredMode)
    {
      this->core = core;
      this->memoryPool = std::make_unique<legit::ShaderMemoryPool>(core->GetDynamicMemoryAlignment());

      presentQueue.reset(new PresentQueue(core, windowDesc, inFlightCount, preferredMode));


      for (size_t frameIndex = 0; frameIndex < inFlightCount; frameIndex++)
      {
        FrameResources frame;
        frame.inFlightFence = core->CreateFence(true);
        frame.imageAcquiredSemaphore = core->CreateVulkanSemaphore();
        frame.renderingFinishedSemaphore = core->CreateVulkanSemaphore();

        frame.commandBuffer = std::move(core->AllocateCommandBuffers(1)[0]);
        frame.shaderMemoryBuffer = std::unique_ptr<legit::Buffer>(new legit::Buffer(core->GetPhysicalDevice(), core->GetLogicalDevice(), 100000000, vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostCoherent));
        frame.gpuProfiler = std::unique_ptr<legit::GpuProfiler>(new legit::GpuProfiler(core->GetPhysicalDevice(), core->GetLogicalDevice(), 512));
        frames.push_back(std::move(frame));
      }
      frameIndex = 0;
    }
    vk::Extent2D GetImageSize()
    {
      return presentQueue->GetImageSize();
    }
    size_t GetInFlightFramesCount()
    {
      return frames.size();
    }
    struct FrameInfo
    {
      legit::ShaderMemoryPool *memoryPool;
      size_t frameIndex;
      legit::RenderGraph::ImageViewProxyId swapchainImageViewProxyId;
    };

    FrameInfo BeginFrame()
    {
      this->profilerFrameId = cpuProfiler.StartFrame();

      auto &currFrame = frames[frameIndex];
      {
        auto fenceTask = cpuProfiler.StartScopedTask("WaitForFence", legit::Colors::pomegranate);
        core->WaitForFence(currFrame.inFlightFence.get());
        core->ResetFence(currFrame.inFlightFence.get());
      }

      {
        auto imageAcquireTask = cpuProfiler.StartScopedTask("ImageAcquire", legit::Colors::emerald);
        currSwapchainImageView = presentQueue->AcquireImage(currFrame.imageAcquiredSemaphore.get());
      }

      {
        auto gpuGatheringTask = cpuProfiler.StartScopedTask("GpuPrfGathering", legit::Colors::amethyst);
        currFrame.gpuProfiler->GatherTimestamps();
      }

      auto &swapchainViewProxyId = swapchainImageViewProxies[currSwapchainImageView];
      if (!swapchainViewProxyId.IsAttached())
      {
        swapchainViewProxyId = core->GetRenderGraph()->AddExternalImageView(currSwapchainImageView);
      }
      core->GetRenderGraph()->AddPass(legit::RenderGraph::FrameSyncPassDesc());

      memoryPool->MapBuffer(currFrame.shaderMemoryBuffer.get());

      FrameInfo frameInfo;
      frameInfo.memoryPool = memoryPool.get();
      frameInfo.frameIndex = frameIndex;
      frameInfo.swapchainImageViewProxyId = swapchainViewProxyId->Id();

      return frameInfo;
    }
    void EndFrame()
    {
      auto &currFrame = frames[frameIndex];

      core->GetRenderGraph()->AddImagePresent(swapchainImageViewProxies[currSwapchainImageView]->Id());

      auto bufferBeginInfo = vk::CommandBufferBeginInfo()
        .setFlags(vk::CommandBufferUsageFlagBits::eSimultaneousUse);
      currFrame.commandBuffer->begin(bufferBeginInfo);
      {
        auto gpuFrame = currFrame.gpuProfiler->StartScopedFrame(currFrame.commandBuffer.get());
        core->GetRenderGraph()->Execute(currFrame.commandBuffer.get(), &cpuProfiler, currFrame.gpuProfiler.get());
      }
      currFrame.commandBuffer->end();

      memoryPool->UnmapBuffer();


      {
        {
          auto presentTask = cpuProfiler.StartScopedTask("Submit", legit::Colors::amethyst);
          vk::Semaphore waitSemaphores[] = { currFrame.imageAcquiredSemaphore.get() };
          vk::Semaphore signalSemaphores[] = { currFrame.renderingFinishedSemaphore.get() };
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
        auto presentTask = cpuProfiler.StartScopedTask("Present", legit::Colors::alizarin);
        presentQueue->PresentImage(currFrame.renderingFinishedSemaphore.get());
      }
      frameIndex = (frameIndex + 1) % frames.size();

      cpuProfiler.EndFrame(profilerFrameId);
      lastFrameCpuProfilerTasks = cpuProfiler.GetProfilerTasks();
    }
    const std::vector<legit::ProfilerTask> &GetLastFrameCpuProfilerData()
    {
      return lastFrameCpuProfilerTasks;
    }
    const std::vector<legit::ProfilerTask> &GetLastFrameGpuProfilerData()
    {
      return frames[frameIndex].gpuProfiler->GetProfilerTasks();
    }
    CpuProfiler &GetCpuProfiler()
    {
      return cpuProfiler;
    }
  private:
    std::unique_ptr<legit::ShaderMemoryPool> memoryPool;
    std::map<legit::ImageView *, legit::RenderGraph::ImageViewProxyUnique> swapchainImageViewProxies;

    struct FrameResources
    {
      vk::UniqueSemaphore imageAcquiredSemaphore;
      vk::UniqueSemaphore renderingFinishedSemaphore;
      vk::UniqueFence inFlightFence;

      vk::UniqueCommandBuffer commandBuffer;
      std::unique_ptr<legit::Buffer> shaderMemoryBuffer;
      std::unique_ptr<legit::GpuProfiler> gpuProfiler;
    };
    std::vector<FrameResources> frames;
    size_t frameIndex;

    legit::Core *core;
    legit::ImageView *currSwapchainImageView;
    std::unique_ptr<PresentQueue> presentQueue;
    legit::CpuProfiler cpuProfiler;
    std::vector<legit::ProfilerTask> lastFrameCpuProfilerTasks;

    size_t profilerFrameId;
  };

  struct ExecuteOnceQueue
  {
    ExecuteOnceQueue(legit::Core *core)
    {
      this->core = core;
      commandBuffer = std::move(core->AllocateCommandBuffers(1)[0]);
    }

    vk::CommandBuffer BeginCommandBuffer()
    {
      auto bufferBeginInfo = vk::CommandBufferBeginInfo()
        .setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
      commandBuffer->begin(bufferBeginInfo);
      return commandBuffer.get();
    }

    void EndCommandBuffer()
    {
      commandBuffer->end();
      vk::PipelineStageFlags waitStages[] = { vk::PipelineStageFlagBits::eAllCommands };

      auto submitInfo = vk::SubmitInfo()
        .setWaitSemaphoreCount(0)
        .setPWaitDstStageMask(waitStages)
        .setCommandBufferCount(1)
        .setPCommandBuffers(&commandBuffer.get())
        .setSignalSemaphoreCount(0);

      core->GetGraphicsQueue().submit({ submitInfo }, nullptr);
      core->GetGraphicsQueue().waitIdle();
    }
  private:
    legit::Core * core;
    vk::UniqueCommandBuffer commandBuffer;
  };
}