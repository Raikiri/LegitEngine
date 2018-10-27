namespace legit
{
  class Core;
  class Buffer
  {
  public:
    vk::Buffer GetHandle()
    {
      return bufferHandle.get();
    }
    vk::DeviceMemory GetMemory()
    {
      return bufferMemory.get();
    }
  private:
    Buffer(vk::PhysicalDevice physicalDevice, vk::Device logicalDevice, vk::DeviceSize size, vk::BufferUsageFlags usageFlags, vk::MemoryPropertyFlags bufferVisibility)
    {
      auto bufferInfo = vk::BufferCreateInfo()
        .setSize(size)
        .setUsage(usageFlags)
        .setSharingMode(vk::SharingMode::eExclusive);
      bufferHandle = logicalDevice.createBufferUnique(bufferInfo);

      vk::MemoryRequirements bufferMemRequirements = logicalDevice.getBufferMemoryRequirements(bufferHandle.get());

      auto allocInfo = vk::MemoryAllocateInfo()
        .setAllocationSize(bufferMemRequirements.size)
        .setMemoryTypeIndex(FindMemoryTypeIndex(physicalDevice, bufferMemRequirements.memoryTypeBits, bufferVisibility));

      bufferMemory = logicalDevice.allocateMemoryUnique(allocInfo);

      logicalDevice.bindBufferMemory(bufferHandle.get(), bufferMemory.get(), 0);
    }
    uint32_t FindMemoryTypeIndex(vk::PhysicalDevice physicalDevice, uint32_t suitableIndices, vk::MemoryPropertyFlags bufferVisibility)
    {
      vk::PhysicalDeviceMemoryProperties availableMemProperties = physicalDevice.getMemoryProperties();

      for (uint32_t i = 0; i < availableMemProperties.memoryTypeCount; i++)
      {
        if ((suitableIndices & (1 << i)) && (availableMemProperties.memoryTypes[i].propertyFlags & bufferVisibility) == bufferVisibility)
        {
          return i;
        }
      }
    }
    vk::UniqueBuffer bufferHandle;
    vk::UniqueDeviceMemory bufferMemory;
    friend class Core;
  };
}