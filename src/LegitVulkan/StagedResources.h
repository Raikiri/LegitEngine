namespace legit
{
  class StagedBuffer
  {
  public:
    StagedBuffer(vk::PhysicalDevice physicalDevice, vk::Device logicalDevice, vk::DeviceSize size, vk::BufferUsageFlags bufferUsage)
    {
      this->size = size;
      stagingBuffer = std::unique_ptr<legit::Buffer>(new legit::Buffer(physicalDevice, logicalDevice, size, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent));
      deviceLocalBuffer = std::unique_ptr<legit::Buffer>(new legit::Buffer(physicalDevice, logicalDevice, size, bufferUsage | vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eDeviceLocal));
    }
    void *Map()
    {
      return stagingBuffer->Map();
    }
    void Unmap(vk::CommandBuffer commandBuffer)
    {
      stagingBuffer->Unmap();
      
      auto copyRegion = vk::BufferCopy()
        .setSrcOffset(0)
        .setDstOffset(0)
        .setSize(size);
      commandBuffer.copyBuffer(stagingBuffer->GetHandle(), deviceLocalBuffer->GetHandle(), { copyRegion });
    }
    vk::Buffer GetBuffer()
    {
      return deviceLocalBuffer->GetHandle();
    }
  private:
    std::unique_ptr<legit::Buffer> stagingBuffer;
    std::unique_ptr<legit::Buffer> deviceLocalBuffer;
    vk::DeviceSize size;
  };

  void LoadBufferData(legit::Core *core, void *bufferData, size_t bufferSize, legit::Buffer *dstBuffer)
  {
    auto stagingBuffer = std::unique_ptr<legit::Buffer>(new legit::Buffer(core->GetPhysicalDevice(), core->GetLogicalDevice(), bufferSize, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent));

    void *bufferMappedData = stagingBuffer->Map();
    memcpy(bufferMappedData, bufferData, bufferSize);
    stagingBuffer->Unmap();

    auto copyRegion = vk::BufferCopy()
      .setSrcOffset(0)
      .setDstOffset(0)
      .setSize(bufferSize);

    legit::ExecuteOnceQueue transferQueue(core);
    auto transferCommandBuffer = transferQueue.BeginCommandBuffer();
    {
      transferCommandBuffer.copyBuffer(stagingBuffer->GetHandle(), dstBuffer->GetHandle(), { copyRegion });
    }
    transferQueue.EndCommandBuffer();
  }


  /*class StagedImage
  {
  public:
    StagedImage(vk::PhysicalDevice physicalDevice, vk::Device logicalDevice, glm::uvec2 size, uint32_t mipsCount, uint32_t arrayLayersCount)
    {
      this->core = core;
      this->memorySize = size.x * size.y * 4; //4bpp
      this->imageSize = size;

      stagingBuffer = std::unique_ptr<legit::Buffer>(new legit::Buffer(physicalDevice, logicalDevice, memorySize, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent));
      deviceLocalImage = std::unique_ptr<legit::Image>(new legit::Image(physicalDevice, logicalDevice, vk::ImageType::e2D, glm::uvec3(size.x, size.y, 1), mipsCount, arrayLayersCount, vk::Format::eR8G8B8A8Unorm, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled));
    }
    void *Map()
    {
      return stagingBuffer->Map();
    }
    void Unmap(vk::CommandBuffer commandBuffer)
    {
      stagingBuffer->Unmap();

      auto imageSubresource = vk::ImageSubresourceLayers()
        .setAspectMask(vk::ImageAspectFlagBits::eColor)
        .setMipLevel(0)
        .setBaseArrayLayer(0)
        .setLayerCount(1);

      auto copyRegion = vk::BufferImageCopy()
        .setBufferOffset(0)
        .setBufferRowLength(0)
        .setBufferImageHeight(0)
        .setImageSubresource(imageSubresource)
        .setImageOffset(vk::Offset3D(0, 0, 0))
        .setImageExtent(vk::Extent3D(imageSize.x, imageSize.y, 1));

      deviceLocalImage->GetData()->TransitionLayout(vk::ImageLayout::eTransferDstOptimal, commandBuffer);
      commandBuffer.copyBufferToImage(stagingBuffer->GetHandle(), deviceLocalImage->GetData()->GetHandle(), vk::ImageLayout::eTransferDstOptimal, { copyRegion });
      deviceLocalImage->GetData()->TransitionLayout(vk::ImageLayout::eShaderReadOnlyOptimal, commandBuffer);
    }
    vk::Image GetImage()
    {
      return deviceLocalImage->GetData()->GetHandle();
    }
  private:
    std::unique_ptr<legit::Buffer> stagingBuffer;
    std::unique_ptr<legit::Image> deviceLocalImage;
    glm::uvec2 imageSize;
    vk::DeviceSize memorySize;
    legit::Core *core;
  };*/
}