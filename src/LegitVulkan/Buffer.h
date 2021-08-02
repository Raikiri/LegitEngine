namespace legit
{
  static uint32_t FindMemoryTypeIndex(vk::PhysicalDevice physicalDevice, uint32_t suitableIndices, vk::MemoryPropertyFlags memoryVisibility)
  {
    vk::PhysicalDeviceMemoryProperties availableMemProperties = physicalDevice.getMemoryProperties();

    for (uint32_t i = 0; i < availableMemProperties.memoryTypeCount; i++)
    {
      if ((suitableIndices & (1 << i)) && (availableMemProperties.memoryTypes[i].propertyFlags & memoryVisibility) == memoryVisibility)
      {
        return i;
      }
    }
    return uint32_t(-1);
  }

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
    void *Map()
    {
      return logicalDevice.mapMemory(GetMemory(), 0, size);
    }
    void Unmap()
    {
      logicalDevice.unmapMemory(GetMemory());
    }
    Buffer(vk::PhysicalDevice physicalDevice, vk::Device logicalDevice, vk::DeviceSize size, vk::BufferUsageFlags usageFlags, vk::MemoryPropertyFlags memoryVisibility)
    {
      this->logicalDevice = logicalDevice;
      this->size = size;
      auto bufferInfo = vk::BufferCreateInfo()
        .setSize(size)
        .setUsage(usageFlags)
        .setSharingMode(vk::SharingMode::eExclusive);
      bufferHandle = logicalDevice.createBufferUnique(bufferInfo);

      vk::MemoryRequirements bufferMemRequirements = logicalDevice.getBufferMemoryRequirements(bufferHandle.get());

      auto allocInfo = vk::MemoryAllocateInfo()
        .setAllocationSize(bufferMemRequirements.size)
        .setMemoryTypeIndex(FindMemoryTypeIndex(physicalDevice, bufferMemRequirements.memoryTypeBits, memoryVisibility));

      bufferMemory = logicalDevice.allocateMemoryUnique(allocInfo);

      logicalDevice.bindBufferMemory(bufferHandle.get(), bufferMemory.get(), 0);
    }
  private:
    vk::UniqueBuffer bufferHandle;
    vk::UniqueDeviceMemory bufferMemory;
    vk::Device logicalDevice;
    vk::DeviceSize size;
    friend class Core;
  };
}