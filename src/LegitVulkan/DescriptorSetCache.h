namespace legit
{
  struct DescriptorSetBindings
  {
    std::vector<UniformBufferBinding> uniformBufferBindings;
    std::vector<ImageSamplerBinding> imageSamplerBindings;
    std::vector<StorageBufferBinding> storageBufferBindings;
    std::vector<StorageImageBinding> storageImageBindings;

    DescriptorSetBindings &SetUniformBufferBindings(std::vector<UniformBufferBinding> uniformBufferBindings)
    {
      this->uniformBufferBindings = uniformBufferBindings;
      return *this;
    }
    DescriptorSetBindings &SetImageSamplerBindings(std::vector<ImageSamplerBinding> imageSamplerBindings)
    {
      this->imageSamplerBindings = imageSamplerBindings;
      return *this;
    }
    DescriptorSetBindings &SetStorageBufferBindings(std::vector<StorageBufferBinding> storageBufferBindings)
    {
      this->storageBufferBindings = storageBufferBindings;
      return *this;
    }
    DescriptorSetBindings &SetStorageImageBindings(std::vector<StorageImageBinding> storageImageBindings)
    {
      this->storageImageBindings = storageImageBindings;
      return *this;
    }
  };
  class DescriptorSetCache
  {
  public:
    DescriptorSetCache(vk::Device _logicalDevice) :
      logicalDevice(_logicalDevice)
    {
      std::vector<vk::DescriptorPoolSize> poolSizes;
      auto uniformPoolSize = vk::DescriptorPoolSize()
        .setDescriptorCount(1000)
        .setType(vk::DescriptorType::eUniformBufferDynamic);
      poolSizes.push_back(uniformPoolSize);

      auto imageSamplerPoolSize = vk::DescriptorPoolSize()
        .setDescriptorCount(1000)
        .setType(vk::DescriptorType::eCombinedImageSampler);
      poolSizes.push_back(imageSamplerPoolSize);

      auto storagePoolSize = vk::DescriptorPoolSize()
        .setDescriptorCount(1000)
        .setType(vk::DescriptorType::eStorageBuffer);
      poolSizes.push_back(storagePoolSize);

      auto storageImagePoolSize = vk::DescriptorPoolSize()
        .setDescriptorCount(1000)
        .setType(vk::DescriptorType::eStorageImage);
      poolSizes.push_back(storageImagePoolSize);

      auto poolCreateInfo = vk::DescriptorPoolCreateInfo()
        .setMaxSets(1000)
        .setPoolSizeCount(uint32_t(poolSizes.size()))
        .setFlags(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet)
        .setPPoolSizes(poolSizes.data());
      descriptorPool = logicalDevice.createDescriptorPoolUnique(poolCreateInfo);
    }
    vk::DescriptorSetLayout GetDescriptorSetLayout(const legit::DescriptorSetLayoutKey &descriptorSetLayoutKey)
    {
      auto &descriptorSetLayout = descriptorSetLayoutCache[descriptorSetLayoutKey];

      if (!descriptorSetLayout)
      {
        std::vector<vk::DescriptorSetLayoutBinding> layoutBindings;

        std::vector<legit::DescriptorSetLayoutKey::UniformBufferId> uniformBufferIds;
        uniformBufferIds.resize(descriptorSetLayoutKey.GetUniformBuffersCount());
        descriptorSetLayoutKey.GetUniformBufferIds(uniformBufferIds.data());

        for (auto uniformBufferId : uniformBufferIds)
        {
          auto bufferInfo = descriptorSetLayoutKey.GetUniformBufferInfo(uniformBufferId);
          auto bufferLayoutBinding = vk::DescriptorSetLayoutBinding()
            .setBinding(bufferInfo.shaderBindingIndex)
            .setDescriptorCount(1) //if this is an array of buffers
            .setDescriptorType(vk::DescriptorType::eUniformBufferDynamic)
            .setStageFlags(bufferInfo.stageFlags);
          layoutBindings.push_back(bufferLayoutBinding);
        }

        std::vector<legit::DescriptorSetLayoutKey::StorageBufferId> storageBufferIds;
        storageBufferIds.resize(descriptorSetLayoutKey.GetStorageBuffersCount());
        descriptorSetLayoutKey.GetStorageBufferIds(storageBufferIds.data());

        for (auto storageBufferId : storageBufferIds)
        {
          auto bufferInfo = descriptorSetLayoutKey.GetStorageBufferInfo(storageBufferId);
          auto bufferLayoutBinding = vk::DescriptorSetLayoutBinding()
            .setBinding(bufferInfo.shaderBindingIndex)
            .setDescriptorCount(1) //if this is an array of buffers
            .setDescriptorType(vk::DescriptorType::eStorageBuffer)
            .setStageFlags(bufferInfo.stageFlags);
          layoutBindings.push_back(bufferLayoutBinding);
        }

        std::vector<legit::DescriptorSetLayoutKey::ImageSamplerId> imageSamplerIds;
        imageSamplerIds.resize(descriptorSetLayoutKey.GetImageSamplersCount());
        descriptorSetLayoutKey.GetImageSamplerIds(imageSamplerIds.data());

        for (auto imageSamplerId : imageSamplerIds)
        {
          auto imageSamplerInfo = descriptorSetLayoutKey.GetImageSamplerInfo(imageSamplerId);

          auto imageSamplerLayoutBinding = vk::DescriptorSetLayoutBinding()
            .setBinding(imageSamplerInfo.shaderBindingIndex)
            .setDescriptorCount(1) //if this is an array of image samplers
            .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
            .setStageFlags(imageSamplerInfo.stageFlags);
          layoutBindings.push_back(imageSamplerLayoutBinding);
        }

        std::vector<legit::DescriptorSetLayoutKey::StorageImageId> storageImageIds;
        storageImageIds.resize(descriptorSetLayoutKey.GetStorageImagesCount());
        descriptorSetLayoutKey.GetStorageImageIds(storageImageIds.data());

        for (auto storageImageId : storageImageIds)
        {
          auto imageInfo = descriptorSetLayoutKey.GetStorageImageInfo(storageImageId);
          auto imageLayoutBinding = vk::DescriptorSetLayoutBinding()
            .setBinding(imageInfo.shaderBindingIndex)
            .setDescriptorCount(1) //if this is an array of buffers
            .setDescriptorType(vk::DescriptorType::eStorageImage)
            .setStageFlags(imageInfo.stageFlags);
          layoutBindings.push_back(imageLayoutBinding);
        }

        auto descriptorLayoutInfo = vk::DescriptorSetLayoutCreateInfo()
          .setBindingCount(uint32_t(layoutBindings.size()))
          .setPBindings(layoutBindings.data());

        descriptorSetLayout = logicalDevice.createDescriptorSetLayoutUnique(descriptorLayoutInfo);
      }
      return descriptorSetLayout.get();
    }

    vk::DescriptorSet GetDescriptorSet(
      legit::DescriptorSetLayoutKey setLayoutKey,
      const std::vector<UniformBufferBinding> &uniformBufferBindings,
      const std::vector<StorageBufferBinding> &storageBufferBindings,
      const std::vector<ImageSamplerBinding> &imageSamplerBindings)
    {
      auto setBindings = legit::DescriptorSetBindings()
        .SetUniformBufferBindings(uniformBufferBindings)
        .SetStorageBufferBindings(storageBufferBindings)
        .SetImageSamplerBindings(imageSamplerBindings);
      return GetDescriptorSet(setLayoutKey, setBindings);
    }
    vk::DescriptorSet GetDescriptorSet(legit::DescriptorSetLayoutKey setLayoutKey, const legit::DescriptorSetBindings &setBindings)
    {
      DescriptorSetKey key;
      key.bindings = setBindings;
      key.layout = GetDescriptorSetLayout(setLayoutKey);
      
      auto &descriptorSet = descriptorSetCache[key];
      if (!descriptorSet)
      {
        auto setAllocInfo = vk::DescriptorSetAllocateInfo()
          .setDescriptorPool(this->descriptorPool.get())
          .setDescriptorSetCount(1)
          .setPSetLayouts(&key.layout);

        descriptorSet = std::move(logicalDevice.allocateDescriptorSetsUnique(setAllocInfo)[0]);

        std::vector<vk::WriteDescriptorSet> setWrites;

        assert(setLayoutKey.GetUniformBuffersCount() == setBindings.uniformBufferBindings.size());
        std::vector<vk::DescriptorBufferInfo> uniformBufferInfos(setBindings.uniformBufferBindings.size()); //cannot be kept in local variable
        for (size_t uniformBufferIndex = 0; uniformBufferIndex < setBindings.uniformBufferBindings.size(); uniformBufferIndex++)
        {
          auto &uniformBinding = setBindings.uniformBufferBindings[uniformBufferIndex];

          {
            auto uniformBufferId = setLayoutKey.GetUniformBufferId(uniformBinding.shaderBindingId);
            assert(uniformBufferId.IsValid());
            auto uniformBufferData = setLayoutKey.GetUniformBufferInfo(uniformBufferId);
            assert(uniformBufferData.size == uniformBinding.size);
          }

          uniformBufferInfos[uniformBufferIndex] = vk::DescriptorBufferInfo()
            .setBuffer(uniformBinding.buffer->GetHandle())
            .setOffset(uniformBinding.offset)
            .setRange(uniformBinding.size);

          auto setWrite = vk::WriteDescriptorSet()
            .setDescriptorCount(1)
            .setDescriptorType(vk::DescriptorType::eUniformBufferDynamic)
            .setDstBinding(uniformBinding.shaderBindingId)
            .setDstSet(descriptorSet.get())
            .setPBufferInfo(&uniformBufferInfos[uniformBufferIndex]);

          setWrites.push_back(setWrite);
        }

        assert(setBindings.imageSamplerBindings.size() == setLayoutKey.GetImageSamplersCount());
        std::vector<vk::DescriptorImageInfo> imageSamplerInfos(setBindings.imageSamplerBindings.size());
        for (size_t imageSamplerIndex = 0; imageSamplerIndex < setBindings.imageSamplerBindings.size(); imageSamplerIndex++)
        {
          auto &imageSamplerBinding = setBindings.imageSamplerBindings[imageSamplerIndex];

          {
            auto imageSamplerId = setLayoutKey.GetImageSamplerId(imageSamplerBinding.shaderBindingId);
            assert(imageSamplerId.IsValid());
            auto imageSamplerData = setLayoutKey.GetImageSamplerInfo(imageSamplerId);
          }

          imageSamplerInfos[imageSamplerIndex] = vk::DescriptorImageInfo()
            .setImageView(imageSamplerBinding.imageView->GetHandle())
            .setSampler(imageSamplerBinding.sampler->GetHandle())
            .setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal);

          //auto imageInfo 
          auto setWrite = vk::WriteDescriptorSet()
            .setDescriptorCount(1)
            .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
            .setDstBinding(imageSamplerBinding.shaderBindingId)
            .setDstSet(descriptorSet.get())
            .setPImageInfo(&imageSamplerInfos[imageSamplerIndex]);

          setWrites.push_back(setWrite);
        }

        assert(setBindings.storageBufferBindings.size() == setLayoutKey.GetStorageBuffersCount());

        std::vector<vk::DescriptorBufferInfo> storageBufferInfos(setBindings.storageBufferBindings.size());

        for (size_t storageBufferIndex = 0; storageBufferIndex < setBindings.storageBufferBindings.size(); storageBufferIndex++)
        {
          auto &storageBinding = setBindings.storageBufferBindings[storageBufferIndex];

          {
            auto storageBufferId = setLayoutKey.GetStorageBufferId(storageBinding.shaderBindingId);
            assert(storageBufferId.IsValid());
            auto storageBufferData = setLayoutKey.GetStorageBufferInfo(storageBufferId);
            //assert(storageBufferData.size == storageBinding.size);
          }

          storageBufferInfos[storageBufferIndex] = vk::DescriptorBufferInfo()
            .setBuffer(storageBinding.buffer->GetHandle())
            .setOffset(storageBinding.offset)
            .setRange(storageBinding.size);

          auto setWrite = vk::WriteDescriptorSet()
            .setDescriptorCount(1)
            .setDescriptorType(vk::DescriptorType::eStorageBuffer)
            .setDstBinding(storageBinding.shaderBindingId)
            .setDstSet(descriptorSet.get())
            .setPBufferInfo(&storageBufferInfos[storageBufferIndex]);

          setWrites.push_back(setWrite);
        }

        assert(setBindings.storageImageBindings.size() == setLayoutKey.GetStorageImagesCount());
        std::vector<vk::DescriptorImageInfo> storageImageInfos(setBindings.storageImageBindings.size());
        for (size_t storageImageIndex = 0; storageImageIndex < setBindings.storageImageBindings.size(); storageImageIndex++)
        {
          auto &storageBinding = setBindings.storageImageBindings[storageImageIndex];

          {
            auto storageImageId = setLayoutKey.GetStorageImageId(storageBinding.shaderBindingId);
            assert(storageImageId.IsValid());
            auto storageImageData = setLayoutKey.GetStorageImageInfo(storageImageId);
            //assert(storageImageData.format == storageBinding.format);
          }

          storageImageInfos[storageImageIndex] = vk::DescriptorImageInfo()
            .setImageView(storageBinding.imageView->GetHandle())
            .setImageLayout(vk::ImageLayout::eGeneral);

          if(setBindings.imageSamplerBindings.size() > 0)
            storageImageInfos[storageImageIndex].setSampler(setBindings.imageSamplerBindings[0].sampler->GetHandle());

          auto setWrite = vk::WriteDescriptorSet()
            .setDescriptorCount(1)
            .setDescriptorType(vk::DescriptorType::eStorageImage)
            .setDstBinding(storageBinding.shaderBindingId)
            .setDstSet(descriptorSet.get())
            .setPImageInfo(&storageImageInfos[storageImageIndex]);

          setWrites.push_back(setWrite);
        }
        logicalDevice.updateDescriptorSets(setWrites, {});
      }
      return descriptorSet.get();
    }
    void Clear()
    {
      this->descriptorSetCache.clear();
      this->descriptorSetLayoutCache.clear();
    }
  private:
    struct DescriptorSetKey
    {
      vk::DescriptorSetLayout layout;
      legit::DescriptorSetBindings bindings;
      bool operator < (const DescriptorSetKey &other) const
      {
        return
          std::tie(layout, bindings.uniformBufferBindings, bindings.storageBufferBindings, bindings.storageImageBindings, bindings.imageSamplerBindings) <
          std::tie(other.layout, other.bindings.uniformBufferBindings, other.bindings.storageBufferBindings, other.bindings.storageImageBindings, other.bindings.imageSamplerBindings);
      }
    };
    std::map<legit::DescriptorSetLayoutKey, vk::UniqueDescriptorSetLayout> descriptorSetLayoutCache;
    vk::UniqueDescriptorPool descriptorPool;
    std::map<DescriptorSetKey, vk::UniqueDescriptorSet> descriptorSetCache;
    vk::Device logicalDevice;
  };
}