#include <map>
#include <set>
#include "spirv_cross.hpp"

namespace legit
{
  class DescriptorSetLayoutKey;
  class Shader;
  class Sampler;
  class ImageView;
  class Buffer;

  template<typename Base>
  struct ShaderResourceId
  {
    ShaderResourceId() :
      id(size_t(-1))
    {
    }

    bool operator ==(const ShaderResourceId<Base> &other) const
    {
      return this->id == other.id;
    }
    bool IsValid()
    {
      return id != size_t(-1);
    }
  private:
    explicit ShaderResourceId(size_t _id) : id(_id) {}
    size_t id;
    friend class DescriptorSetLayoutKey;
    friend class Shader;
  };

  struct ImageSamplerBinding
  {
    ImageSamplerBinding() : imageView(nullptr), sampler(nullptr) {}
    ImageSamplerBinding(legit::ImageView *_imageView, legit::Sampler *_sampler, uint32_t _shaderBindingId) : imageView(_imageView), sampler(_sampler), shaderBindingId(_shaderBindingId)
    {
      assert(_imageView);
      assert(_sampler);
    }
    bool operator < (const ImageSamplerBinding &other) const
    {
      return std::tie(imageView, sampler, shaderBindingId) < std::tie(other.imageView, other.sampler, other.shaderBindingId);
    }
    legit::ImageView *imageView;
    legit::Sampler *sampler;
    uint32_t shaderBindingId;
  };

  struct UniformBufferBinding
  {
    UniformBufferBinding() : buffer(nullptr), offset(-1), size(-1) {}
    UniformBufferBinding(legit::Buffer *_buffer, uint32_t _shaderBindingId, vk::DeviceSize _offset, vk::DeviceSize _size) : buffer(_buffer), shaderBindingId(_shaderBindingId), offset(_offset), size(_size)
    {
      assert(_buffer);
    }
    bool operator < (const UniformBufferBinding &other) const
    {
      return std::tie(buffer, shaderBindingId, offset, size) < std::tie(other.buffer, other.shaderBindingId, other.offset, other.size);
    }
    legit::Buffer *buffer;
    uint32_t shaderBindingId;
    vk::DeviceSize offset;
    vk::DeviceSize size;
  };

  struct StorageBufferBinding
  {
    StorageBufferBinding() : buffer(nullptr), offset(-1), size(-1) {}
    StorageBufferBinding(legit::Buffer *_buffer, uint32_t _shaderBindingId, vk::DeviceSize _offset, vk::DeviceSize _size) : buffer(_buffer), shaderBindingId(_shaderBindingId), offset(_offset), size(_size)
    {
      assert(_buffer);
    }

    bool operator < (const StorageBufferBinding &other) const
    {
      return std::tie(buffer, shaderBindingId, offset, size) < std::tie(other.buffer, other.shaderBindingId, other.offset, other.size);
    }
    legit::Buffer *buffer;
    uint32_t shaderBindingId;
    vk::DeviceSize offset;
    vk::DeviceSize size;
  };

  struct StorageImageBinding
  {
    StorageImageBinding() : imageView(nullptr) {}
    StorageImageBinding(legit::ImageView *_imageView, uint32_t _shaderBindingId) : imageView(_imageView), shaderBindingId(_shaderBindingId)
    {
      assert(_imageView);
    }
    bool operator < (const StorageImageBinding &other) const
    {
      return std::tie(imageView, shaderBindingId) < std::tie(other.imageView, other.shaderBindingId);
    }
    legit::ImageView *imageView;
    uint32_t shaderBindingId;
  };

  class DescriptorSetLayoutKey
  {
  public:
    struct UniformBase;
    using UniformId = ShaderResourceId<UniformBase>;
    struct UniformBufferBase;
    using UniformBufferId = ShaderResourceId<UniformBufferBase>;
    struct ImageSamplerBase;
    using ImageSamplerId = ShaderResourceId<ImageSamplerBase>;
    struct StorageBufferBase;
    using StorageBufferId = ShaderResourceId<StorageBufferBase>;
    struct StorageImageBase;
    using StorageImageId = ShaderResourceId<StorageImageBase>;


    struct UniformData
    {
      bool operator<(const UniformData &other) const
      {
        return std::tie(name, offsetInBinding, size) < std::tie(other.name, other.offsetInBinding, other.size);
      }
      std::string name;
      uint32_t offsetInBinding;
      uint32_t size;
      UniformBufferId uniformBufferId;
    };
    struct UniformBufferData
    {
      bool operator<(const UniformBufferData &other) const
      {
        return std::tie(name, shaderBindingIndex, size) < std::tie(other.name, other.shaderBindingIndex, other.size);
      }

      std::string name;
      uint32_t shaderBindingIndex;
      vk::ShaderStageFlags stageFlags;

      uint32_t size;
      uint32_t offsetInSet;
      std::vector<UniformId> uniformIds;
    };
    struct ImageSamplerData
    {
      bool operator<(const ImageSamplerData &other) const
      {
        return std::tie(name, shaderBindingIndex) < std::tie(other.name, other.shaderBindingIndex);
      }

      std::string name;
      uint32_t shaderBindingIndex;
      vk::ShaderStageFlags stageFlags;
    };
    struct StorageBufferData
    {
      bool operator<(const StorageBufferData &other) const
      {
        return std::tie(name, shaderBindingIndex, size) < std::tie(other.name, other.shaderBindingIndex, other.size);
      }

      std::string name;
      uint32_t shaderBindingIndex;
      vk::ShaderStageFlags stageFlags;

      uint32_t size;
      uint32_t offsetInSet;
    };
    struct StorageImageData
    {
      bool operator<(const StorageImageData &other) const
      {
        return std::tie(name, shaderBindingIndex) < std::tie(other.name, other.shaderBindingIndex);
      }

      std::string name;
      uint32_t shaderBindingIndex;
      vk::ShaderStageFlags stageFlags;
    };

    size_t GetUniformBuffersCount() const
    {
      return uniformBufferDatum.size();
    }
    void GetUniformBufferIds(UniformBufferId *dstUniformBufferIds, size_t count = -1, size_t offset = 0) const
    {
      if (count == -1)
        count = uniformBufferDatum.size();
      assert(count + offset <= uniformBufferDatum.size());
      for (size_t index = offset; index < offset + count; index++)
        dstUniformBufferIds[index] = UniformBufferId(index);
    }
    UniformBufferId GetUniformBufferId(std::string bufferName) const
    {
      auto it = uniformBufferNameToIds.find(bufferName);
      if (it == uniformBufferNameToIds.end())
        return UniformBufferId();
      return it->second;
    }
    UniformBufferId GetUniformBufferId(uint32_t bufferBindingId) const
    {
      auto it = uniformBufferBindingToIds.find(bufferBindingId);
      if (it == uniformBufferBindingToIds.end())
        return UniformBufferId();
      return it->second;
    }    
    UniformBufferData GetUniformBufferInfo(UniformBufferId uniformBufferId) const
    {
      return uniformBufferDatum[uniformBufferId.id];
    }
    UniformBufferBinding MakeUniformBufferBinding(std::string bufferName, legit::Buffer *_buffer, vk::DeviceSize _offset = 0, vk::DeviceSize _size = VK_WHOLE_SIZE) const
    {
      auto uniformBufferId = GetUniformBufferId(bufferName);
      assert(uniformBufferId.IsValid());
      auto uniformBufferInfo = GetUniformBufferInfo(uniformBufferId);
      return UniformBufferBinding(_buffer, uniformBufferInfo.shaderBindingIndex, _offset, _size);
    }


    size_t GetStorageBuffersCount() const
    {
      return storageBufferDatum.size();
    }
    void GetStorageBufferIds(StorageBufferId *dstStorageBufferIds, size_t count = -1, size_t offset = 0) const
    {
      if (count == -1)
        count = storageBufferDatum.size();
      assert(count + offset <= storageBufferDatum.size());
      for (size_t index = offset; index < offset + count; index++)
        dstStorageBufferIds[index] = StorageBufferId(index);
    }
    StorageBufferId GetStorageBufferId(std::string storageBufferName) const
    {
      auto it = storageBufferNameToIds.find(storageBufferName);
      if (it == storageBufferNameToIds.end())
        return StorageBufferId();
      return it->second;
    }
    StorageBufferId GetStorageBufferId(uint32_t bufferBindingId) const
    {
      auto it = storageBufferBindingToIds.find(bufferBindingId);
      if (it == storageBufferBindingToIds.end())
        return StorageBufferId();
      return it->second;
    }
    StorageBufferData GetStorageBufferInfo(StorageBufferId storageBufferId) const
    {
      return storageBufferDatum[storageBufferId.id];
    }
    StorageBufferBinding MakeStorageBufferBinding(std::string bufferName, legit::Buffer *_buffer, vk::DeviceSize _offset = 0, vk::DeviceSize _size = VK_WHOLE_SIZE) const
    {
      auto storageBufferId = GetStorageBufferId(bufferName);
      assert(storageBufferId.IsValid());
      auto storageBufferInfo = GetStorageBufferInfo(storageBufferId);
      return StorageBufferBinding(_buffer, storageBufferInfo.shaderBindingIndex, _offset, _size);
    }


    size_t GetUniformsCount() const
    {
      return uniformDatum.size();
    }
    void GetUniformIds(UniformId *dstUniformIds, size_t count = -1, size_t offset = 0) const
    {
      if (count == -1)
        count = uniformDatum.size();
      assert(count + offset <= uniformDatum.size());
      for (size_t index = offset; index < offset + count; index++)
        dstUniformIds[index] = UniformId(index);
    }
    UniformData GetUniformInfo(UniformId uniformId) const
    {
      return uniformDatum[uniformId.id];
    }
    UniformId GetUniformId(std::string name) const
    {
      auto it = uniformNameToIds.find(name);
      if (it == uniformNameToIds.end())
        return UniformId();
      return it->second;
    }

    size_t GetImageSamplersCount() const
    {
      return imageSamplerDatum.size();
    }
    void GetImageSamplerIds(ImageSamplerId *dstImageSamplerIds, size_t count = -1, size_t offset = 0) const
    {
      if (count == -1)
        count = imageSamplerDatum.size();
      assert(count + offset <= imageSamplerDatum.size());
      for (size_t index = offset; index < offset + count; index++)
        dstImageSamplerIds[index] = ImageSamplerId(index);
    }
    ImageSamplerData GetImageSamplerInfo(ImageSamplerId imageSamplerId) const
    {
      return imageSamplerDatum[imageSamplerId.id];
    }
    ImageSamplerId GetImageSamplerId(std::string imageSamplerName) const
    {
      auto it = imageSamplerNameToIds.find(imageSamplerName);
      if (it == imageSamplerNameToIds.end())
        return ImageSamplerId();
      return it->second;
    }
    ImageSamplerId GetImageSamplerId(uint32_t shaderBindingIndex) const
    {
      auto it = imageSamplerBindingToIds.find(shaderBindingIndex);
      if (it == imageSamplerBindingToIds.end())
        return ImageSamplerId();
      return it->second;
    }
    ImageSamplerBinding MakeImageSamplerBinding(std::string imageSamplerName, legit::ImageView *_imageView, legit::Sampler *_sampler) const
    {
      auto imageSamplerId = GetImageSamplerId(imageSamplerName);
      assert(imageSamplerId.IsValid());
      auto imageSamplerInfo = GetImageSamplerInfo(imageSamplerId);
      return ImageSamplerBinding(_imageView, _sampler, imageSamplerInfo.shaderBindingIndex);
    }

    size_t GetStorageImagesCount() const
    {
      return storageImageDatum.size();
    }
    void GetStorageImageIds(StorageImageId *dstStorageImageIds, size_t count = -1, size_t offset = 0) const
    {
      if (count == -1)
        count = storageImageDatum.size();
      assert(count + offset <= storageImageDatum.size());
      for (size_t index = offset; index < offset + count; index++)
        dstStorageImageIds[index] = StorageImageId(index);
    }
    StorageImageId GetStorageImageId(std::string storageImageName) const
    {
      auto it = storageImageNameToIds.find(storageImageName);
      if (it == storageImageNameToIds.end())
        return StorageImageId();
      return it->second;
    }
    StorageImageId GetStorageImageId(uint32_t bufferBindingId) const
    {
      auto it = storageImageBindingToIds.find(bufferBindingId);
      if (it == storageImageBindingToIds.end())
        return StorageImageId();
      return it->second;
    }
    StorageImageData GetStorageImageInfo(StorageImageId storageImageId) const
    {
      return storageImageDatum[storageImageId.id];
    }
    StorageImageBinding MakeStorageImageBinding(std::string imageName, legit::ImageView *_imageView) const
    {
      auto imageId = GetStorageImageId(imageName);
      assert(imageId.IsValid());
      auto storageImageInfo = GetStorageImageInfo(imageId);
      return StorageImageBinding(_imageView, storageImageInfo.shaderBindingIndex);
    }

    uint32_t GetTotalConstantBufferSize() const
    {
      return size;
    }

    bool IsEmpty() const
    {
      return GetImageSamplersCount() == 0 && GetUniformBuffersCount() == 0 && GetStorageImagesCount() == 0 && GetStorageBuffersCount() == 0;
    }

    bool operator<(const DescriptorSetLayoutKey &other) const
    {
      return std::tie(uniformDatum, uniformBufferDatum, imageSamplerDatum, storageBufferDatum, storageImageDatum) < std::tie(other.uniformDatum, other.uniformBufferDatum, other.imageSamplerDatum, other.storageBufferDatum, other.storageImageDatum);
    }

    static DescriptorSetLayoutKey Merge(DescriptorSetLayoutKey *setLayouts, size_t setsCount)
    {
      DescriptorSetLayoutKey res;
      if (setsCount <= 0) return res;

      res.setShaderId = setLayouts[0].setShaderId;
      for (size_t layoutIndex = 0; layoutIndex < setsCount; layoutIndex++)
        assert(setLayouts[layoutIndex].setShaderId == res.setShaderId);

      std::set<uint32_t> uniformBufferBindings;
      std::set<uint32_t> imageSamplerBindings;
      std::set<uint32_t> storageBufferBindings;
      std::set<uint32_t> storageImageBindings;

      for (size_t setIndex = 0; setIndex < setsCount; setIndex++)
      {
        auto &setLayout = setLayouts[setIndex];
        for (auto &uniformBufferData : setLayout.uniformBufferDatum)
        {
          uniformBufferBindings.insert(uniformBufferData.shaderBindingIndex);
        }
        for (auto &imageSamplerData : setLayout.imageSamplerDatum)
        {
          imageSamplerBindings.insert(imageSamplerData.shaderBindingIndex);
        }
        for (auto &storageBufferData : setLayout.storageBufferDatum)
        {
          storageBufferBindings.insert(storageBufferData.shaderBindingIndex);
        }
        for (auto &storageImageData : setLayout.storageImageDatum)
        {
          storageImageBindings.insert(storageImageData.shaderBindingIndex);
        }
      }

      for (auto &uniformBufferBinding : uniformBufferBindings)
      {
        UniformBufferId dstUniformBufferId;
        for (size_t setIndex = 0; setIndex < setsCount; setIndex++)
        {
          auto &srcLayout = setLayouts[setIndex];
          auto srcUniformBufferId = srcLayout.GetUniformBufferId(uniformBufferBinding);
          if (!srcUniformBufferId.IsValid()) continue;
          const auto &srcUniformBuffer = srcLayout.uniformBufferDatum[srcUniformBufferId.id];
          assert(srcUniformBuffer.shaderBindingIndex == uniformBufferBinding);

          if (!dstUniformBufferId.IsValid())
          {
            dstUniformBufferId = UniformBufferId(res.uniformBufferDatum.size());
            res.uniformBufferDatum.push_back(UniformBufferData());
            auto &dstUniformBuffer = res.uniformBufferDatum.back();

            dstUniformBuffer.shaderBindingIndex = srcUniformBuffer.shaderBindingIndex;
            dstUniformBuffer.name = srcUniformBuffer.name;
            dstUniformBuffer.size = srcUniformBuffer.size;
            dstUniformBuffer.stageFlags = srcUniformBuffer.stageFlags;

            for (auto srcUniformId : srcUniformBuffer.uniformIds)
            {
              auto srcUniformData = srcLayout.GetUniformInfo(srcUniformId);

              DescriptorSetLayoutKey::UniformId dstUniformId = DescriptorSetLayoutKey::UniformId(res.uniformDatum.size());
              res.uniformDatum.push_back(DescriptorSetLayoutKey::UniformData());
              DescriptorSetLayoutKey::UniformData &dstUniformData = res.uniformDatum.back();

              dstUniformData.uniformBufferId = dstUniformBufferId;
              dstUniformData.offsetInBinding = srcUniformData.offsetInBinding;
              dstUniformData.size = srcUniformData.size;
              dstUniformData.name = srcUniformData.name;

              //memberData.
              dstUniformBuffer.uniformIds.push_back(dstUniformId);
            }
          }
          else
          {
            auto &dstUniformBuffer = res.uniformBufferDatum[dstUniformBufferId.id];
            dstUniformBuffer.stageFlags |= srcUniformBuffer.stageFlags;
            assert(srcUniformBuffer.shaderBindingIndex == dstUniformBuffer.shaderBindingIndex);
            assert(srcUniformBuffer.name == dstUniformBuffer.name);
            assert(srcUniformBuffer.size == dstUniformBuffer.size);
          }
        }
      }

     

      for (auto &imageSamplerBinding : imageSamplerBindings)
      {
        ImageSamplerId dstImageSamplerId;
        for (size_t setIndex = 0; setIndex < setsCount; setIndex++)
        {
          auto &srcLayout = setLayouts[setIndex];
          auto srcImageSamplerId = srcLayout.GetImageSamplerId(imageSamplerBinding);
          if (!srcImageSamplerId.IsValid()) continue;
          const auto &srcImageSampler = srcLayout.imageSamplerDatum[srcImageSamplerId.id];
          assert(srcImageSampler.shaderBindingIndex == imageSamplerBinding);

          if (!dstImageSamplerId.IsValid())
          {
            dstImageSamplerId = ImageSamplerId(res.imageSamplerDatum.size());
            res.imageSamplerDatum.push_back(ImageSamplerData());
            auto &dstImageSampler = res.imageSamplerDatum.back();

            dstImageSampler.shaderBindingIndex = srcImageSampler.shaderBindingIndex;
            dstImageSampler.name = srcImageSampler.name;
            dstImageSampler.stageFlags = srcImageSampler.stageFlags;
          }
          else
          {
            auto &dstImageSampler = res.imageSamplerDatum[dstImageSamplerId.id];
            dstImageSampler.stageFlags |= srcImageSampler.stageFlags;
            assert(srcImageSampler.shaderBindingIndex == dstImageSampler.shaderBindingIndex);
            assert(srcImageSampler.name == dstImageSampler.name);
          }
        }
      }
      for (auto &storageBufferBinding : storageBufferBindings)
      {
        StorageBufferId dstStorageBufferId;
        for (size_t setIndex = 0; setIndex < setsCount; setIndex++)
        {
          auto &srcLayout = setLayouts[setIndex];
          auto srcStorageBufferId = srcLayout.GetStorageBufferId(storageBufferBinding);
          if (!srcStorageBufferId.IsValid()) continue;
          const auto &srcStorageBuffer = srcLayout.storageBufferDatum[srcStorageBufferId.id];
          assert(srcStorageBuffer.shaderBindingIndex == storageBufferBinding);

          if (!dstStorageBufferId.IsValid())
          {
            dstStorageBufferId = StorageBufferId(res.storageBufferDatum.size());
            res.storageBufferDatum.push_back(StorageBufferData());
            auto &dstStorageBuffer = res.storageBufferDatum.back();

            dstStorageBuffer.shaderBindingIndex = srcStorageBuffer.shaderBindingIndex;
            dstStorageBuffer.name = srcStorageBuffer.name;
            dstStorageBuffer.size = srcStorageBuffer.size;
            dstStorageBuffer.stageFlags = srcStorageBuffer.stageFlags;
          }
          else
          {
            auto &dstStorageBuffer = res.storageBufferDatum[dstStorageBufferId.id];
            dstStorageBuffer.stageFlags |= srcStorageBuffer.stageFlags;
            assert(srcStorageBuffer.shaderBindingIndex == dstStorageBuffer.shaderBindingIndex);
            assert(srcStorageBuffer.name == dstStorageBuffer.name);
            assert(srcStorageBuffer.size == dstStorageBuffer.size);
          }
        }
      }
      
      for (auto &storageImageBinding : storageImageBindings)
      {
        StorageImageId dstStorageImageId;
        for (size_t setIndex = 0; setIndex < setsCount; setIndex++)
        {
          auto &srcLayout = setLayouts[setIndex];
          auto srcStorageImageId = srcLayout.GetStorageImageId(storageImageBinding);
          if (!srcStorageImageId.IsValid()) continue;
          const auto &srcStorageImage = srcLayout.storageImageDatum[srcStorageImageId.id];
          assert(srcStorageImage.shaderBindingIndex == storageImageBinding);

          if (!dstStorageImageId.IsValid())
          {
            dstStorageImageId = StorageImageId(res.storageImageDatum.size());
            res.storageImageDatum.push_back(StorageImageData());
            auto &dstStorageImage = res.storageImageDatum.back();

            dstStorageImage.shaderBindingIndex = srcStorageImage.shaderBindingIndex;
            dstStorageImage.name = srcStorageImage.name;
            dstStorageImage.stageFlags = srcStorageImage.stageFlags;
          }
          else
          {
            auto &dstStorageImage = res.storageImageDatum[dstStorageImageId.id];
            dstStorageImage.stageFlags |= srcStorageImage.stageFlags;
            assert(srcStorageImage.shaderBindingIndex == dstStorageImage.shaderBindingIndex);
            assert(srcStorageImage.name == dstStorageImage.name);
          }
        }
      }


      res.RebuildIndex();
      return res;
    }
  private:
    void RebuildIndex()
    {
      uniformNameToIds.clear();
      for(size_t uniformIndex = 0; uniformIndex < uniformDatum.size(); uniformIndex++)
      {
        UniformId uniformId = UniformId(uniformIndex);
        auto &uniformData = uniformDatum[uniformIndex];
        uniformNameToIds[uniformData.name] = uniformId;
      }

      uniformBufferNameToIds.clear();
      uniformBufferBindingToIds.clear();
      this->size = 0;
      for (size_t uniformBufferIndex = 0; uniformBufferIndex < uniformBufferDatum.size(); uniformBufferIndex++)
      {
        UniformBufferId uniformBufferId = UniformBufferId(uniformBufferIndex);
        auto &uniformBufferData = uniformBufferDatum[uniformBufferIndex];
        uniformBufferNameToIds[uniformBufferData.name] = uniformBufferId;
        uniformBufferBindingToIds[uniformBufferData.shaderBindingIndex] = uniformBufferId;
        this->size += uniformBufferData.size;
      }

      imageSamplerNameToIds.clear();
      imageSamplerBindingToIds.clear();
      for (size_t imageSamplerIndex = 0; imageSamplerIndex < imageSamplerDatum.size(); imageSamplerIndex++)
      {
        ImageSamplerId imageSamplerId = ImageSamplerId(imageSamplerIndex);
        auto &imageSamplerData = imageSamplerDatum[imageSamplerIndex];
        imageSamplerNameToIds[imageSamplerData.name] = imageSamplerId;
        imageSamplerBindingToIds[imageSamplerData.shaderBindingIndex] = imageSamplerId;
      }

      storageBufferNameToIds.clear();
      storageBufferBindingToIds.clear();
      for (size_t storageBufferIndex = 0; storageBufferIndex < storageBufferDatum.size(); storageBufferIndex++)
      {
        StorageBufferId storageBufferId = StorageBufferId(storageBufferIndex);
        auto &storageBufferData = storageBufferDatum[storageBufferIndex];
        storageBufferNameToIds[storageBufferData.name] = storageBufferId;
        storageBufferBindingToIds[storageBufferData.shaderBindingIndex] = storageBufferId;
      }

      storageImageNameToIds.clear();
      storageImageBindingToIds.clear();
      for (size_t storageImageIndex = 0; storageImageIndex < storageImageDatum.size(); storageImageIndex++)
      {
        StorageImageId storageImageId = StorageImageId(storageImageIndex);
        auto &storageImageData = storageImageDatum[storageImageIndex];
        storageImageNameToIds[storageImageData.name] = storageImageId;
        storageImageBindingToIds[storageImageData.shaderBindingIndex] = storageImageId;
      }
    }


    friend class Shader;
    uint32_t setShaderId;
    uint32_t size;

    std::vector<UniformData> uniformDatum;
    std::vector<UniformBufferData> uniformBufferDatum;
    std::vector<ImageSamplerData> imageSamplerDatum;
    std::vector<StorageBufferData> storageBufferDatum;
    std::vector<StorageImageData> storageImageDatum;

    std::map<std::string, UniformId> uniformNameToIds;
    std::map<std::string, UniformBufferId> uniformBufferNameToIds;
    std::map<uint32_t, UniformBufferId> uniformBufferBindingToIds;
    std::map<std::string, ImageSamplerId> imageSamplerNameToIds;
    std::map<uint32_t, ImageSamplerId> imageSamplerBindingToIds;
    std::map<std::string, StorageBufferId> storageBufferNameToIds;
    std::map<uint32_t, StorageBufferId> storageBufferBindingToIds;
    std::map<std::string, StorageImageId> storageImageNameToIds;
    std::map<uint32_t, StorageImageId> storageImageBindingToIds;
  };
  class Shader
  {
  public:
    Shader(vk::Device logicalDevice, std::string shaderFile)
    {
      auto bytecode = GetBytecode(shaderFile);
      /*vk::ShaderStageFlagBits shaderStage;
      if (shaderFile.find(".vert.spv") != std::string::npos)
        shaderStage = vk::ShaderStageFlagBits::eVertex;
      if (shaderFile.find(".frag.spv") != std::string::npos)
        shaderStage = vk::ShaderStageFlagBits::eFragment;
      if (shaderFile.find(".comp.spv") != std::string::npos)
        shaderStage = vk::ShaderStageFlagBits::eCompute;*/
      Init(logicalDevice, bytecode);
    }
    Shader(vk::Device logicalDevice, const std::vector<uint32_t> &bytecode)
    {
      Init(logicalDevice, bytecode);
    }
    static const std::vector<uint32_t> GetBytecode(std::string filename)
    {
      std::ifstream file(filename, std::ios::ate | std::ios::binary);

      if (!file.is_open())
        throw std::runtime_error("failed to open file!");

      size_t fileSize = (size_t)file.tellg();
      std::vector<uint32_t> bytecode(fileSize / sizeof(uint32_t));

      file.seekg(0);
      file.read((char*)bytecode.data(), bytecode.size() * sizeof(uint32_t));
      file.close();
      return bytecode;
    }

    legit::ShaderModule *GetModule()
    {
      return shaderModule.get();
    }


    size_t GetSetsCount()
    {
      return descriptorSetLayoutKeys.size();
    }
    const DescriptorSetLayoutKey *GetSetInfo(size_t setIndex)
    {
      return &descriptorSetLayoutKeys[setIndex];
    }
    glm::uvec3 GetLocalSize()
    {
      return localSize;
    }
  private:
    void Init(vk::Device logicalDevice, const std::vector<uint32_t> &bytecode)
    {
      shaderModule.reset(new ShaderModule(logicalDevice, bytecode));
      localSize = glm::uvec3(0);
      spirv_cross::Compiler compiler(bytecode.data(), bytecode.size());

      std::vector<spirv_cross::EntryPoint> entryPoints = compiler.get_entry_points_and_stages();
      assert(entryPoints.size() == 1);
      vk::ShaderStageFlags stageFlags;
      switch (entryPoints[0].execution_model)
      {
        case spv::ExecutionModel::ExecutionModelVertex:
        {
          stageFlags |= vk::ShaderStageFlagBits::eVertex;
        }break;
        case spv::ExecutionModel::ExecutionModelFragment:
        {
          stageFlags |= vk::ShaderStageFlagBits::eFragment;
        }break;
        case spv::ExecutionModel::ExecutionModelGLCompute:
        {
          stageFlags |= vk::ShaderStageFlagBits::eCompute;
          localSize.x = compiler.get_execution_mode_argument(spv::ExecutionMode::ExecutionModeLocalSize, 0);
          localSize.y = compiler.get_execution_mode_argument(spv::ExecutionMode::ExecutionModeLocalSize, 1);
          localSize.z = compiler.get_execution_mode_argument(spv::ExecutionMode::ExecutionModeLocalSize, 2);
        }break;
      }

      spirv_cross::ShaderResources resources = compiler.get_shader_resources();


      struct SetResources
      {
        std::vector<spirv_cross::Resource> uniformBuffers;
        std::vector<spirv_cross::Resource> imageSamplers;
        std::vector<spirv_cross::Resource> storageBuffers;
        std::vector<spirv_cross::Resource> storageImages;
      };
      std::vector<SetResources> setResources;
      for (const auto &buffer : resources.uniform_buffers)
      {
        uint32_t setShaderId = compiler.get_decoration(buffer.id, spv::DecorationDescriptorSet);
        if (setShaderId >= setResources.size())
          setResources.resize(setShaderId + 1);
        setResources[setShaderId].uniformBuffers.push_back(buffer);
      }

      for (const auto &imageSampler : resources.sampled_images)
      {
        uint32_t setShaderId = compiler.get_decoration(imageSampler.id, spv::DecorationDescriptorSet);
        if (setShaderId >= setResources.size())
          setResources.resize(setShaderId + 1);
        setResources[setShaderId].imageSamplers.push_back(imageSampler);
      }
      
      for (const auto &buffer : resources.storage_buffers)
      {
        uint32_t setShaderId = compiler.get_decoration(buffer.id, spv::DecorationDescriptorSet);
        if (setShaderId >= setResources.size())
          setResources.resize(setShaderId + 1);
        setResources[setShaderId].storageBuffers.push_back(buffer);
      }

      for (const auto &image : resources.storage_images)
      {
        uint32_t setShaderId = compiler.get_decoration(image.id, spv::DecorationDescriptorSet);
        if (setShaderId >= setResources.size())
          setResources.resize(setShaderId + 1);
        setResources[setShaderId].storageImages.push_back(image);
      }

      this->descriptorSetLayoutKeys.resize(setResources.size());
      for (size_t setIndex = 0; setIndex < setResources.size(); setIndex++)
      {
        auto &descriptorSetLayoutKey = descriptorSetLayoutKeys[setIndex];
        descriptorSetLayoutKey = DescriptorSetLayoutKey();
        descriptorSetLayoutKey.setShaderId = uint32_t(setIndex);

        for (auto buffer : setResources[setIndex].uniformBuffers)
        {
          uint32_t shaderBindingIndex = compiler.get_decoration(buffer.id, spv::DecorationBinding);

          auto bufferType = compiler.get_type(buffer.type_id);

          if (bufferType.basetype == spirv_cross::SPIRType::BaseType::Struct)
          {
            auto uniformBufferId = DescriptorSetLayoutKey::UniformBufferId(descriptorSetLayoutKey.uniformBufferDatum.size());
            descriptorSetLayoutKey.uniformBufferDatum.emplace_back(DescriptorSetLayoutKey::UniformBufferData());
            auto &bufferData = descriptorSetLayoutKey.uniformBufferDatum.back();

            bufferData.shaderBindingIndex = shaderBindingIndex;
            bufferData.name = buffer.name;
            bufferData.stageFlags = stageFlags;

            uint32_t declaredSize = uint32_t(compiler.get_declared_struct_size(bufferType));

            uint32_t currOffset = 0;
            for (uint32_t memberIndex = 0; memberIndex < bufferType.member_types.size(); memberIndex++)
            {
              uint32_t memberSize = uint32_t(compiler.get_declared_struct_member_size(bufferType, memberIndex));
              //auto memberType = compiler.get_type(bufferType.member_types[memberIndex]);
              auto memberName = compiler.get_member_name(buffer.base_type_id, memberIndex);

              //memberData.size = compiler.get_declared_struct_size(memeberType);
              DescriptorSetLayoutKey::UniformId uniformId = DescriptorSetLayoutKey::UniformId(descriptorSetLayoutKey.uniformDatum.size());
              descriptorSetLayoutKey.uniformDatum.push_back(DescriptorSetLayoutKey::UniformData());
              DescriptorSetLayoutKey::UniformData &uniformData = descriptorSetLayoutKey.uniformDatum.back();

              uniformData.uniformBufferId = uniformBufferId;
              uniformData.offsetInBinding = uint32_t(currOffset);
              uniformData.size = uint32_t(memberSize);
              uniformData.name = memberName;

              //memberData.
              bufferData.uniformIds.push_back(uniformId);

              currOffset += memberSize;
            }
            assert(currOffset == declaredSize); //alignment is wrong. avoid using smaller types before larger ones. completely avoid vec2/vec3
            bufferData.size = declaredSize;
            bufferData.offsetInSet = descriptorSetLayoutKey.size;

            descriptorSetLayoutKey.size += bufferData.size;
          }
        }

        for (auto imageSampler : setResources[setIndex].imageSamplers)
        {
          auto imageSamplerId = DescriptorSetLayoutKey::ImageSamplerId(descriptorSetLayoutKey.imageSamplerDatum.size());

          uint32_t shaderBindingIndex = compiler.get_decoration(imageSampler.id, spv::DecorationBinding);
          descriptorSetLayoutKey.imageSamplerDatum.push_back(DescriptorSetLayoutKey::ImageSamplerData());
          auto &imageSamplerData = descriptorSetLayoutKey.imageSamplerDatum.back();
          imageSamplerData.shaderBindingIndex = shaderBindingIndex;
          imageSamplerData.stageFlags = stageFlags;
          imageSamplerData.name = imageSampler.name;
        }

        for (auto buffer : setResources[setIndex].storageBuffers)
        {
          uint32_t shaderBindingIndex = compiler.get_decoration(buffer.id, spv::DecorationBinding);

          auto bufferType = compiler.get_type(buffer.type_id);

          if (bufferType.basetype == spirv_cross::SPIRType::BaseType::Struct)
          {
            auto storageBufferId = DescriptorSetLayoutKey::StorageBufferId(descriptorSetLayoutKey.storageBufferDatum.size());
            descriptorSetLayoutKey.storageBufferDatum.emplace_back(DescriptorSetLayoutKey::StorageBufferData());
            auto &bufferData = descriptorSetLayoutKey.storageBufferDatum.back();

            bufferData.shaderBindingIndex = shaderBindingIndex;
            bufferData.stageFlags = stageFlags;
            bufferData.name = buffer.name;

            size_t declaredSize = compiler.get_declared_struct_size(bufferType);

            uint32_t currOffset = 0;

            assert(currOffset == declaredSize); //alignment is wrong. avoid using smaller types before larger ones. completely avoid vec2/vec3
            bufferData.size = currOffset;
            bufferData.offsetInSet = descriptorSetLayoutKey.size;

            descriptorSetLayoutKey.size += bufferData.size;
          }
        }

        for (auto image : setResources[setIndex].storageImages)
        {
          auto imageSamplerId = DescriptorSetLayoutKey::ImageSamplerId(descriptorSetLayoutKey.imageSamplerDatum.size());

          uint32_t shaderBindingIndex = compiler.get_decoration(image.id, spv::DecorationBinding);
          descriptorSetLayoutKey.storageImageDatum.push_back(DescriptorSetLayoutKey::StorageImageData());
          auto &storageImageData = descriptorSetLayoutKey.storageImageDatum.back();
          storageImageData.shaderBindingIndex = shaderBindingIndex;
          storageImageData.stageFlags = stageFlags;
          storageImageData.name = image.name;
          //type?
        }
        descriptorSetLayoutKey.RebuildIndex();
      }
    }


    std::vector<DescriptorSetLayoutKey> descriptorSetLayoutKeys;

    std::unique_ptr<legit::ShaderModule> shaderModule;
    glm::uvec3 localSize;
  };

  class ShaderProgram
  {
  public:
    ShaderProgram(Shader *_vertexShader, Shader *_fragmentShader)
      : vertexShader(_vertexShader), fragmentShader(_fragmentShader)
    {
      combinedDescriptorSetLayoutKeys.resize(std::max(vertexShader->GetSetsCount(), fragmentShader->GetSetsCount()));
      for (size_t setIndex = 0; setIndex < combinedDescriptorSetLayoutKeys.size(); setIndex++)
      {

        vk::DescriptorSetLayout setLayoutHandle = nullptr;
        std::vector<legit::DescriptorSetLayoutKey> setLayoutStageKeys;

        if (setIndex < vertexShader->GetSetsCount())
        {
          auto vertexSetInfo = vertexShader->GetSetInfo(setIndex);
          if (!vertexSetInfo->IsEmpty())
          {
            setLayoutStageKeys.push_back(*vertexSetInfo);
          }
        }
        if (setIndex < fragmentShader->GetSetsCount())
        {
          auto fragmentSetInfo = fragmentShader->GetSetInfo(setIndex);
          if (!fragmentSetInfo->IsEmpty())
          {
            setLayoutStageKeys.push_back(*fragmentSetInfo);
          }
        }
        this->combinedDescriptorSetLayoutKeys[setIndex] = DescriptorSetLayoutKey::Merge(setLayoutStageKeys.data(), setLayoutStageKeys.size());
      }
    }

    size_t GetSetsCount()
    {
      return combinedDescriptorSetLayoutKeys.size();
    }
    const DescriptorSetLayoutKey *GetSetInfo(size_t setIndex)
    {
      return &combinedDescriptorSetLayoutKeys[setIndex];
    }

    std::vector<DescriptorSetLayoutKey> combinedDescriptorSetLayoutKeys;
    Shader *vertexShader;
    Shader *fragmentShader;
  };
}