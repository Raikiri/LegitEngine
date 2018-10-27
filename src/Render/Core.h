#include <iostream>

#include <vector>
#include <set>
namespace legit
{
  class Swapchain;
  class RenderStateCache;

  class Core
  {
  public:    
    Core(const char **instanceExtensions, uint32_t instanceExtensionsCount, WindowDesc *compatibleWindowDesc, bool enableDebugging)
    {
      std::vector<const char*> resIntanceExtensions(instanceExtensions, instanceExtensions + instanceExtensionsCount);
      std::vector<const char*> validationLayers;
      if (enableDebugging)
      {
        validationLayers.push_back("VK_LAYER_LUNARG_standard_validation");
        resIntanceExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
      }

      this->instance = CreateInstance(resIntanceExtensions, validationLayers);
      loader = vk::DispatchLoaderDynamic(instance.get());

      if(enableDebugging)
        this->debugUtilsMessenger = CreateDebugUtilsMessenger(instance.get(), DebugMessageCallback, loader);
      this->physicalDevice = FindPhysicalDevice(instance.get());
      
      {
        vk::UniqueSurfaceKHR compatibleSurface;
        if (compatibleWindowDesc)
          compatibleSurface = CreateWin32Surface(instance.get(), *compatibleWindowDesc);
        this->queueFamilyIndices = FindQueueFamilyIndices(physicalDevice, compatibleSurface.get());
      }
      
      std::vector<const char*> deviceExtensions;
      deviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
      this->logicalDevice = CreateLogicalDevice(physicalDevice, queueFamilyIndices, deviceExtensions, validationLayers);
      this->graphicsQueue = GetDeviceQueue(logicalDevice.get(), queueFamilyIndices.graphicsFamilyIndex);
      this->presentQueue = GetDeviceQueue(logicalDevice.get(), queueFamilyIndices.presentFamilyIndex);
      this->commandPool = CreateCommandPool(logicalDevice.get(), queueFamilyIndices.graphicsFamilyIndex);
    }
    ~Core()
    {
    }
    std::unique_ptr<Swapchain> CreateSwapchain(WindowDesc windowDesc)
    {
      auto swapchain = std::unique_ptr<Swapchain>(new Swapchain(instance.get(), physicalDevice, logicalDevice.get(), windowDesc, queueFamilyIndices));
      return swapchain;
    }
    std::unique_ptr<ShaderModule> CreateShaderModule(std::string filename)
    {
      return std::unique_ptr<ShaderModule>(new ShaderModule(this->logicalDevice.get(), filename));
    }
    std::unique_ptr<RenderPass> CreateRenderPass(std::vector<vk::Format> formats)
    {
      return std::unique_ptr<RenderPass>(new RenderPass(this->logicalDevice.get(), formats));
    }
    std::unique_ptr<Pipeline> CreatePipeline(vk::ShaderModule vertexShader, vk::ShaderModule fragmentShader, const legit::VertexDeclaration &vertexDecl, vk::RenderPass renderPass)
    {
      return std::unique_ptr<Pipeline>(new Pipeline(this->logicalDevice.get(), vertexShader, fragmentShader, vertexDecl, renderPass));
    }
    std::unique_ptr<Framebuffer> CreateFramebuffer(const std::vector <const legit::ImageView * > &imageViews, vk::Extent2D size, vk::RenderPass renderPass)
    {
      return std::unique_ptr<Framebuffer>(new Framebuffer(this->logicalDevice.get(), imageViews, size, renderPass));
    }
    std::unique_ptr<legit::Buffer> CreateBuffer(vk::DeviceSize size, vk::BufferUsageFlags usageFlags, vk::MemoryPropertyFlags bufferVisibility)
    {
      return std::unique_ptr<legit::Buffer>(new legit::Buffer(physicalDevice, logicalDevice.get(), size, usageFlags, bufferVisibility));
    }
    vk::CommandPool GetCommandPool()
    {
      return commandPool.get();
    }
    vk::Device GetLogicalDevice()
    {
      return logicalDevice.get();
    }
    std::vector<vk::UniqueCommandBuffer> AllocateCommandBuffers(size_t count)
    {
      auto commandBufferAllocateInfo = vk::CommandBufferAllocateInfo()
        .setCommandPool(commandPool.get())
        .setLevel(vk::CommandBufferLevel::ePrimary)
        .setCommandBufferCount(uint32_t(count));
      
      return logicalDevice->allocateCommandBuffersUnique(commandBufferAllocateInfo);
    }
    vk::UniqueSemaphore CreateVulkanSemaphore()
    {
      auto semaphoreInfo = vk::SemaphoreCreateInfo();
      return logicalDevice->createSemaphoreUnique(semaphoreInfo);
    }
    vk::UniqueFence CreateFence(bool state)
    {
      auto fenceInfo = vk::FenceCreateInfo();
      if(state)
        fenceInfo.setFlags(vk::FenceCreateFlagBits::eSignaled);
      return logicalDevice->createFenceUnique(fenceInfo);
    }
    void WaitForFence(vk::Fence fence)
    {
      logicalDevice->waitForFences({ fence }, true, std::numeric_limits<uint64_t>::max());
    }
    void ResetFence(vk::Fence fence)
    {
      logicalDevice->resetFences({ fence });
    }
    void WaitIdle()
    {
      logicalDevice->waitIdle();
    }
    vk::Queue GetGraphicsQueue()
    {
      return graphicsQueue;
    }
    vk::Queue GetPresentQueue()
    {
      return presentQueue;
    }
  private:

    static vk::UniqueInstance CreateInstance(const std::vector<const char*> &instanceExtensions, const std::vector<const char*> &validationLayers)
    {
      auto appInfo = vk::ApplicationInfo()
        .setPApplicationName("Legit app")
        .setApplicationVersion(VK_MAKE_VERSION(-1, 0, 0))
        .setPEngineName("Legit engine")
        .setEngineVersion(VK_MAKE_VERSION(-1, 0, 0))
        .setApiVersion(VK_API_VERSION_1_0);

      auto instanceCreateInfo = vk::InstanceCreateInfo()
        .setPApplicationInfo(&appInfo)
        .setEnabledExtensionCount(uint32_t(instanceExtensions.size()))
        .setPpEnabledExtensionNames(instanceExtensions.data())
        .setEnabledLayerCount(uint32_t(validationLayers.size()))
        .setPpEnabledLayerNames(validationLayers.data());

      return vk::createInstanceUnique(instanceCreateInfo);
    }

    static vk::UniqueHandle<vk::DebugUtilsMessengerEXT, vk::DispatchLoaderDynamic> CreateDebugUtilsMessenger(vk::Instance instance, PFN_vkDebugUtilsMessengerCallbackEXT debugCallback, vk::DispatchLoaderDynamic &loader)
    {
      auto messengerCreateInfo = vk::DebugUtilsMessengerCreateInfoEXT()
        .setMessageSeverity(vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose | vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError/* | vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo*/)
        .setMessageType(vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation)
        .setPfnUserCallback(debugCallback)
        .setPUserData(nullptr); // Optional

      return instance.createDebugUtilsMessengerEXTUnique(messengerCreateInfo, nullptr, loader);
    }

    static VKAPI_ATTR VkBool32 VKAPI_CALL DebugMessageCallback(
      VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
      VkDebugUtilsMessageTypeFlagsEXT messageType,
      const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
      void* pUserData) 
    {
      std::cerr << "validation layer: " << pCallbackData->pMessage << std::endl;

      return VK_FALSE;
    }

    friend class Swapchain;
    static vk::PhysicalDevice FindPhysicalDevice(vk::Instance instance)
    {
      std::vector<vk::PhysicalDevice> physicalDevices = instance.enumeratePhysicalDevices();

      vk::PhysicalDevice physicalDevice = nullptr;
      for (const auto& device : physicalDevices) 
      {
        vk::PhysicalDeviceProperties deviceProperties = device.getProperties();
        vk::PhysicalDeviceFeatures deviceFeatures = device.getFeatures();
        
        if(deviceProperties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu)
        {
          return device;
        }
      }
      throw std::runtime_error("Failed to find physical device");
      return nullptr;
    }

    static QueueFamilyIndices FindQueueFamilyIndices(vk::PhysicalDevice physicalDevice, vk::SurfaceKHR surface)
    {
      std::vector<vk::QueueFamilyProperties> queueFamilies = physicalDevice.getQueueFamilyProperties();

      QueueFamilyIndices queueFamilyIndices;
      queueFamilyIndices.graphicsFamilyIndex = uint32_t(-1);
      queueFamilyIndices.presentFamilyIndex = uint32_t(-1);
      for (uint32_t familyIndex = 0; familyIndex < queueFamilies.size(); familyIndex++)
      {
        if (queueFamilies[familyIndex].queueFlags & vk::QueueFlagBits::eGraphics && queueFamilies[familyIndex].queueCount > 0 && queueFamilyIndices.graphicsFamilyIndex == uint32_t(-1))
          queueFamilyIndices.graphicsFamilyIndex = familyIndex;

        if(physicalDevice.getSurfaceSupportKHR(familyIndex, surface) && queueFamilies[familyIndex].queueCount > 0 && queueFamilyIndices.presentFamilyIndex == uint32_t(-1))
          queueFamilyIndices.presentFamilyIndex = familyIndex;
      }
      if(queueFamilyIndices.graphicsFamilyIndex == uint32_t(-1))
        throw std::runtime_error("Failed to find appropriate queue families");
      return queueFamilyIndices;
    }

    static vk::UniqueDevice CreateLogicalDevice(vk::PhysicalDevice physicalDevice, QueueFamilyIndices familyIndices, std::vector<const char*> deviceExtensions, std::vector<const char*> validationLayers)
    {
      std::set<uint32_t> uniqueQueueFamilyIndices = { familyIndices.graphicsFamilyIndex, familyIndices.presentFamilyIndex };

      std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos;
      float queuePriority = 1.0f;
      for (uint32_t queueFamily : uniqueQueueFamilyIndices) {
        auto queueCreateInfo = vk::DeviceQueueCreateInfo()
          .setQueueFamilyIndex(queueFamily)
          .setQueueCount(1)
          .setPQueuePriorities(&queuePriority);

        queueCreateInfos.push_back(queueCreateInfo);
      }

      vk::PhysicalDeviceFeatures deviceFeatures = {};

      auto deviceCreateInfo = vk::DeviceCreateInfo()
        .setQueueCreateInfoCount(uint32_t(queueCreateInfos.size()))
        .setPQueueCreateInfos(queueCreateInfos.data())
        .setPEnabledFeatures(&deviceFeatures)
        .setEnabledExtensionCount(uint32_t(deviceExtensions.size()))
        .setPpEnabledExtensionNames(deviceExtensions.data())
        .setEnabledLayerCount(uint32_t(validationLayers.size()))
        .setPpEnabledLayerNames(validationLayers.data());

      return physicalDevice.createDeviceUnique(deviceCreateInfo);
    }
    static vk::Queue GetDeviceQueue(vk::Device logicalDevice, uint32_t queueFamilyIndex)
    {
      return logicalDevice.getQueue(queueFamilyIndex, 0);
    }
    static vk::UniqueCommandPool CreateCommandPool(vk::Device logicalDevice, uint32_t familyIndex)
    {
      auto commandPoolInfo = vk::CommandPoolCreateInfo()
        //.setFlags(vk::CommandPoolCreateFlagBits::eTransient)
        .setFlags(vk::CommandPoolCreateFlagBits::eResetCommandBuffer)
        .setQueueFamilyIndex(familyIndex);
      return logicalDevice.createCommandPoolUnique(commandPoolInfo);
    }

    vk::UniqueInstance instance;
    vk::DispatchLoaderDynamic loader;
    vk::UniqueHandle<vk::DebugUtilsMessengerEXT, vk::DispatchLoaderDynamic> debugUtilsMessenger;
    vk::PhysicalDevice physicalDevice;
    vk::UniqueDevice logicalDevice;
    vk::UniqueCommandPool commandPool;
    vk::Queue graphicsQueue;
    vk::Queue presentQueue;

    QueueFamilyIndices queueFamilyIndices;
  };
}