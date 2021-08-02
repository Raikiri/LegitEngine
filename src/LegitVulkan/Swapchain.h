namespace legit
{
  class Swapchain
  {
  public:
    vk::Format GetFormat()
    {
      return this->surfaceFormat.format;
    }
    vk::Extent2D GetSize()
    {
      return extent;
    }
    std::vector<ImageView *> GetImageViews()
    {
      std::vector<ImageView *> resImageViews;
      for (auto &image : this->images)
        resImageViews.push_back(image.imageView.get());
      return resImageViews;
    }
    vk::ResultValue<uint32_t> AcquireNextImage(vk::Semaphore semaphore)
    {
      return logicalDevice.acquireNextImageKHR(swapchain.get(), std::numeric_limits<uint64_t>::max(), semaphore, nullptr);
    }
    vk::SwapchainKHR GetHandle()
    {
      return swapchain.get();
    }
  private:
    Swapchain(vk::Instance instance, vk::PhysicalDevice physicalDevice, vk::Device logicalDevice, WindowDesc windowDesc, uint32_t imagesCount, QueueFamilyIndices queueFamilyIndices, vk::PresentModeKHR preferredMode)
    {
      this->logicalDevice = logicalDevice;
      this->surface = legit::CreateWin32Surface(instance, windowDesc);
      if (queueFamilyIndices.presentFamilyIndex == uint32_t(-1) || !physicalDevice.getSurfaceSupportKHR(queueFamilyIndices.presentFamilyIndex, surface.get()))
        throw std::runtime_error("Window surface is incompatible with device");

      this->surfaceDetails = GetSurfaceDetails(physicalDevice, surface.get());

      this->surfaceFormat = FindSwapchainSurfaceFormat(surfaceDetails.formats);
      this->presentMode = FindSwapchainPresentMode(surfaceDetails.presentModes, preferredMode);
      this->extent = FindSwapchainExtent(surfaceDetails.capabilities, vk::Extent2D(100, 100));

      uint32_t imageCount = std::max(surfaceDetails.capabilities.minImageCount, imagesCount);
      if (surfaceDetails.capabilities.maxImageCount > 0 && imageCount > surfaceDetails.capabilities.maxImageCount)
        imageCount = surfaceDetails.capabilities.maxImageCount;

      auto swapchainCreateInfo = vk::SwapchainCreateInfoKHR()
        .setSurface(surface.get())
        .setMinImageCount(imageCount)
        .setImageFormat(surfaceFormat.format)
        .setImageColorSpace(surfaceFormat.colorSpace)
        .setImageExtent(extent)
        .setImageArrayLayers(1)
        .setImageUsage(vk::ImageUsageFlagBits::eColorAttachment)
        .setPreTransform(surfaceDetails.capabilities.currentTransform)
        .setCompositeAlpha(vk::CompositeAlphaFlagBitsKHR::eOpaque)
        .setPresentMode(presentMode)
        .setClipped(true)
        .setOldSwapchain(nullptr);

      uint32_t familyIndices[] = { queueFamilyIndices.graphicsFamilyIndex, queueFamilyIndices.presentFamilyIndex };

      if (queueFamilyIndices.graphicsFamilyIndex != queueFamilyIndices.presentFamilyIndex)
      {
        swapchainCreateInfo
          .setImageSharingMode(vk::SharingMode::eConcurrent)
          .setQueueFamilyIndexCount(2)
          .setPQueueFamilyIndices(familyIndices);
      }
      else 
      {
        swapchainCreateInfo
          .setImageSharingMode(vk::SharingMode::eExclusive);
      }
      this->swapchain = logicalDevice.createSwapchainKHRUnique(swapchainCreateInfo);

      std::vector<vk::Image> swapchainImages = logicalDevice.getSwapchainImagesKHR(swapchain.get());

      this->images.clear();
      for (size_t imageIndex = 0; imageIndex < swapchainImages.size(); imageIndex++)
      {
        Image newbie;
        newbie.imageData = std::unique_ptr<ImageData>(new ImageData(swapchainImages[imageIndex], vk::ImageType::e2D, glm::vec3(extent.width, extent.height, 1), 1, 1, surfaceFormat.format, vk::ImageLayout::eUndefined));
        newbie.imageView = std::unique_ptr<ImageView>(new ImageView(logicalDevice, newbie.imageData.get(), 0, 1, 0, 1));

        this->images.emplace_back(std::move(newbie));
      }
    }

    struct SurfaceDetails
    {
      vk::SurfaceCapabilitiesKHR capabilities;
      std::vector<vk::SurfaceFormatKHR> formats;
      std::vector<vk::PresentModeKHR> presentModes;
    };
    static SurfaceDetails GetSurfaceDetails(vk::PhysicalDevice physicalDevice, vk::SurfaceKHR surface)
    {
      SurfaceDetails surfaceDetails;
      surfaceDetails.capabilities = physicalDevice.getSurfaceCapabilitiesKHR(surface);
      surfaceDetails.formats = physicalDevice.getSurfaceFormatsKHR(surface);
      surfaceDetails.presentModes = physicalDevice.getSurfacePresentModesKHR(surface);

      return surfaceDetails;
    }
    static vk::SurfaceFormatKHR FindSwapchainSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& availableFormats)
    {
      vk::SurfaceFormatKHR bestFormat = { vk::Format::eB8G8R8A8Srgb, vk::ColorSpaceKHR::eSrgbNonlinear };
      if (availableFormats.size() == 1 && availableFormats[0].format == vk::Format::eUndefined)
      {
        return bestFormat;
      }

      for (const auto& availableFormat : availableFormats)
      {
        if (availableFormat.format == bestFormat.format && availableFormat.colorSpace == bestFormat.colorSpace)
          return availableFormat;
      }
      throw std::runtime_error("No suitable format found");
      return bestFormat;
    }

    static vk::PresentModeKHR FindSwapchainPresentMode(const std::vector<vk::PresentModeKHR> availablePresentModes, vk::PresentModeKHR preferredMode)
    {
      for (const auto& availablePresentMode : availablePresentModes)
      {
        //if (availablePresentMode == vk::PresentModeKHR::eMailbox)
        if (availablePresentMode == preferredMode)
          return availablePresentMode;
      }

      return vk::PresentModeKHR::eFifo;
    }

    static vk::Extent2D FindSwapchainExtent(const vk::SurfaceCapabilitiesKHR& capabilities, vk::Extent2D windowSize)
    {
      if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
      {
        return capabilities.currentExtent;
      }
      else
      {
        vk::Extent2D actualExtent = windowSize;

        actualExtent.width = std::max(capabilities.minImageExtent.width, std::min(capabilities.maxImageExtent.width, actualExtent.width));
        actualExtent.height = std::max(capabilities.minImageExtent.height, std::min(capabilities.maxImageExtent.height, actualExtent.height));

        return actualExtent;
      }
    }

    SurfaceDetails surfaceDetails;
    vk::Device logicalDevice;
    vk::SurfaceFormatKHR surfaceFormat;
    vk::PresentModeKHR presentMode;
    vk::Extent2D extent;
    struct Image
    {
      std::unique_ptr<legit::ImageData> imageData;
      std::unique_ptr<legit::ImageView> imageView;
    };
    std::vector<Image> images;


    vk::UniqueSurfaceKHR surface;
    vk::UniqueSwapchainKHR swapchain;


    friend class Core;
  };
}