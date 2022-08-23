#include <iostream>

#include <vector>
#include <set>
namespace legit
{
  Core::Core(const char **instanceExtensions, uint32_t instanceExtensionsCount, WindowDesc *compatibleWindowDesc, bool enableDebugging)
  {
    std::vector<const char*> resIntanceExtensions(instanceExtensions, instanceExtensions + instanceExtensionsCount);
    std::vector<const char*> validationLayers;
    if (enableDebugging)
    {
      validationLayers.push_back("VK_LAYER_KHRONOS_validation");
      resIntanceExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    this->instance = CreateInstance(resIntanceExtensions, validationLayers);
    //loader = vk::DispatchLoaderDynamic();
    loader = vk::DispatchLoaderDynamic(instance.get(), vkGetInstanceProcAddr);

    auto prop = vk::enumerateInstanceLayerProperties();
    
    if(enableDebugging)
      this->debugUtilsMessenger = CreateDebugUtilsMessenger(instance.get(), DebugMessageCallback, loader);
    this->physicalDevice = FindPhysicalDevice(instance.get());
    
    /*std::cout << "Supported extensions:\n";
    auto extensions = physicalDevice.enumerateDeviceExtensionProperties();
    for (auto extension : extensions)
    {
      std::cout << "  " << extension.extensionName << "\n";
    }*/

    if(compatibleWindowDesc)
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

    this->descriptorSetCache.reset(new legit::DescriptorSetCache(logicalDevice.get()));
    this->pipelineCache.reset(new legit::PipelineCache(logicalDevice.get(), this->descriptorSetCache.get()));

    this->renderGraph.reset(new legit::RenderGraph(physicalDevice, logicalDevice.get(), loader));
  }
  Core::~Core()
  {
  }
  void Core::ClearCaches()
  {
    this->descriptorSetCache->Clear();
    this->pipelineCache->Clear();
  }
  std::unique_ptr<Swapchain> Core::CreateSwapchain(WindowDesc windowDesc, uint32_t imagesCount, vk::PresentModeKHR preferredMode)
  {
    auto swapchain = std::unique_ptr<Swapchain>(new Swapchain(instance.get(), physicalDevice, logicalDevice.get(), windowDesc, imagesCount, queueFamilyIndices, preferredMode));
    return swapchain;
  }
    
  void Core::SetDebugName(legit::ImageData *imageData, std::string name)
  {
    SetObjectDebugName(imageData->GetHandle(), name);
    imageData->SetDebugName(name);
  }

  vk::CommandPool Core::GetCommandPool()
  {
    return commandPool.get();
  }
  vk::Device Core::GetLogicalDevice()
  {
    return logicalDevice.get();
  }
  vk::PhysicalDevice Core::GetPhysicalDevice()
  {
    return physicalDevice;
  }
  legit::RenderGraph *Core::GetRenderGraph()
  {
    return renderGraph.get();
  }
  std::vector<vk::UniqueCommandBuffer> Core::AllocateCommandBuffers(size_t count)
  {
    auto commandBufferAllocateInfo = vk::CommandBufferAllocateInfo()
      .setCommandPool(commandPool.get())
      .setLevel(vk::CommandBufferLevel::ePrimary)
      .setCommandBufferCount(uint32_t(count));
    
    return logicalDevice->allocateCommandBuffersUnique(commandBufferAllocateInfo);
  }
  vk::UniqueSemaphore Core::CreateVulkanSemaphore()
  {
    auto semaphoreInfo = vk::SemaphoreCreateInfo();
    return logicalDevice->createSemaphoreUnique(semaphoreInfo);
  }
  vk::UniqueFence Core::CreateFence(bool state)
  {
    auto fenceInfo = vk::FenceCreateInfo();
    if(state)
      fenceInfo.setFlags(vk::FenceCreateFlagBits::eSignaled);
    return logicalDevice->createFenceUnique(fenceInfo);
  }
  void Core::WaitForFence(vk::Fence fence)
  {
    auto res = logicalDevice->waitForFences({ fence }, true, std::numeric_limits<uint64_t>::max());
  }
  void Core::ResetFence(vk::Fence fence)
  {
    logicalDevice->resetFences({ fence });
  }
  void Core::WaitIdle()
  {
    logicalDevice->waitIdle();
  }
  vk::Queue Core::GetGraphicsQueue()
  {
    return graphicsQueue;
  }
  vk::Queue Core::GetPresentQueue()
  {
    return presentQueue;
  }
  uint32_t Core::GetDynamicMemoryAlignment()
  {
    return uint32_t(physicalDevice.getProperties().limits.minUniformBufferOffsetAlignment);
  }
  legit::DescriptorSetCache *Core::GetDescriptorSetCache()
  {
    return descriptorSetCache.get();
  }
  legit::PipelineCache *Core::GetPipelineCache()
  {
    return pipelineCache.get();
  }
  vk::UniqueInstance Core::CreateInstance(const std::vector<const char*> &instanceExtensions, const std::vector<const char*> &validationLayers)
  {
    auto appInfo = vk::ApplicationInfo()
      .setPApplicationName("Legit app")
      .setApplicationVersion(VK_MAKE_VERSION(-1, 0, 0))
      .setPEngineName("Legit engine")
      .setEngineVersion(VK_MAKE_VERSION(-1, 0, 0))
      .setApiVersion(VK_API_VERSION_1_2);

    auto instanceCreateInfo = vk::InstanceCreateInfo()
      .setPApplicationInfo(&appInfo)
      .setEnabledExtensionCount(uint32_t(instanceExtensions.size()))
      .setPpEnabledExtensionNames(instanceExtensions.data())
      .setEnabledLayerCount(uint32_t(validationLayers.size()))
      .setPpEnabledLayerNames(validationLayers.data());

    return vk::createInstanceUnique(instanceCreateInfo);
  }

  vk::UniqueHandle<vk::DebugUtilsMessengerEXT, vk::DispatchLoaderDynamic> Core::CreateDebugUtilsMessenger(vk::Instance instance, PFN_vkDebugUtilsMessengerCallbackEXT debugCallback, vk::DispatchLoaderDynamic &loader)
  {
    auto messengerCreateInfo = vk::DebugUtilsMessengerCreateInfoEXT()
      .setMessageSeverity(vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError/* | vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo*/)
      .setMessageType(vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation)
      .setPfnUserCallback(debugCallback)
      .setPUserData(nullptr); // Optional

    return instance.createDebugUtilsMessengerEXTUnique(messengerCreateInfo, nullptr, loader);
  }

  VKAPI_ATTR VkBool32 VKAPI_CALL Core::DebugMessageCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData) 
  {
    std::cerr << "validation layer: " << pCallbackData->pMessage << std::endl;

    return VK_FALSE;
  }

  vk::PhysicalDevice Core::FindPhysicalDevice(vk::Instance instance)
  {
    std::vector<vk::PhysicalDevice> physicalDevices = instance.enumeratePhysicalDevices();
    std::cout << "Found " << physicalDevices.size() << " physical device(s)\n";
    vk::PhysicalDevice physicalDevice = nullptr;
    for (const auto& device : physicalDevices) 
    {
      vk::PhysicalDeviceProperties deviceProperties = device.getProperties();
      std::cout << "  Physical device found: " << deviceProperties.deviceName;
      vk::PhysicalDeviceFeatures deviceFeatures = device.getFeatures();

      if(deviceProperties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu)
      {
        physicalDevice = device;
        std::cout << " <-- Using this device";
      }
      std::cout << "\n";
    }
    if(!physicalDevice)
      throw std::runtime_error("Failed to find physical device");
    return physicalDevice;
  }

  QueueFamilyIndices Core::FindQueueFamilyIndices(vk::PhysicalDevice physicalDevice, vk::SurfaceKHR surface)
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

  vk::UniqueDevice Core::CreateLogicalDevice(vk::PhysicalDevice physicalDevice, QueueFamilyIndices familyIndices, std::vector<const char*> deviceExtensions, std::vector<const char*> validationLayers)
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

    auto deviceFeatures = vk::PhysicalDeviceFeatures()
      .setFragmentStoresAndAtomics(true)
      .setVertexPipelineStoresAndAtomics(true);

    auto deviceCreateInfo = vk::DeviceCreateInfo()
      .setQueueCreateInfoCount(uint32_t(queueCreateInfos.size()))
      .setPQueueCreateInfos(queueCreateInfos.data())
      .setPEnabledFeatures(&deviceFeatures)
      .setEnabledExtensionCount(uint32_t(deviceExtensions.size()))
      .setPpEnabledExtensionNames(deviceExtensions.data())
      .setEnabledLayerCount(uint32_t(validationLayers.size()))
      .setPpEnabledLayerNames(validationLayers.data());

    auto deviceFeatures12 = vk::PhysicalDeviceVulkan12Features()
      .setScalarBlockLayout(true);

    vk::StructureChain<vk::DeviceCreateInfo, vk::PhysicalDeviceVulkan12Features> chain = { deviceCreateInfo , deviceFeatures12 };

    return physicalDevice.createDeviceUnique(chain.get<vk::DeviceCreateInfo>());
  }
  vk::Queue Core::GetDeviceQueue(vk::Device logicalDevice, uint32_t queueFamilyIndex)
  {
    return logicalDevice.getQueue(queueFamilyIndex, 0);
  }
  vk::UniqueCommandPool Core::CreateCommandPool(vk::Device logicalDevice, uint32_t familyIndex)
  {
    auto commandPoolInfo = vk::CommandPoolCreateInfo()
      //.setFlags(vk::CommandPoolCreateFlagBits::eTransient)
      .setFlags(vk::CommandPoolCreateFlagBits::eResetCommandBuffer)
      .setQueueFamilyIndex(familyIndex);
    return logicalDevice.createCommandPoolUnique(commandPoolInfo);
  }
}