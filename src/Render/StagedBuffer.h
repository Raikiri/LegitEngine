namespace legit
{
  class StagedBuffer
  {
  public:
    StagedBuffer(legit::Core *core, vk::DeviceSize size, vk::BufferUsageFlags bufferUsage)
    {
      this->core = core;
      this->size = size;
      stagingBuffer = core->CreateBuffer(size, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
      deviceLocalBuffer = core->CreateBuffer(size, bufferUsage | vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eDeviceLocal);
    }
    void *Map()
    {
      return core->GetLogicalDevice().mapMemory(stagingBuffer->GetMemory(), 0, size);
    }
    void Unmap(vk::CommandBuffer commandBuffer)
    {
      core->GetLogicalDevice().unmapMemory(stagingBuffer->GetMemory());
      auto copyRegion = vk::BufferCopy()
        .setSrcOffset(0)
        .setDstOffset(0)
        .setSize(size);
      commandBuffer.copyBuffer(stagingBuffer->GetHandle(), deviceLocalBuffer->GetHandle(), {copyRegion});
    }
    vk::Buffer GetBuffer()
    {
      return deviceLocalBuffer->GetHandle();
    }
  private:
    std::unique_ptr<legit::Buffer> stagingBuffer;
    std::unique_ptr<legit::Buffer> deviceLocalBuffer;
    vk::DeviceSize size;
    legit::Core *core;
  };
}