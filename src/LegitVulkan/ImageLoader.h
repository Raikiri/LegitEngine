#include <gli/gli.hpp>

namespace legit
{
  struct ImageTexelData
  {
    size_t layersCount = 0;
    vk::Format format = vk::Format::eUndefined;
    size_t texelSize = 0;
    glm::uvec3 baseSize = { 0, 0, 0 };

    struct Mip
    {
      glm::uvec3 size;

      struct Layer
      {
        size_t offset;
      };
      std::vector<Layer> layers;
    };
    std::vector<Mip> mips;
    std::vector<uint8_t> texels;
  };

  static ImageTexelData CreateTestCubeTexelData()
  {
    glm::uvec3 size = { 64, 64, 1 };
    ImageTexelData texelData;
    texelData.layersCount = 6;
    texelData.format = vk::Format::eR8G8B8A8Snorm;
    texelData.texelSize = 4;
    texelData.mips.resize(1);
    texelData.mips[0].layers.resize(texelData.layersCount);
    texelData.baseSize = size;

    texelData.texels.resize(size.x * size.y * texelData.layersCount * texelData.texelSize);

    size_t currOffset = 0;
    for (auto &mip : texelData.mips)
    {
      mip.size = size;
      for (auto &layer : mip.layers)
      {
        layer.offset = currOffset;
        for (size_t z = 0; z < size.z; z++)
        {
          for (size_t y = 0; y < size.y; y++)
          {
            for (size_t x = 0; x < size.x; x++)
            {
              uint8_t *texelPtr = texelData.texels.data() + currOffset + (x + y * mip.size.x + z * mip.size.x * mip.size.y) * texelData.texelSize;
              texelPtr[0] = uint8_t(x);
              texelPtr[1] = uint8_t(y);
              texelPtr[2] = 0;
              texelPtr[3] = 0;
            }
          }
        }
        currOffset += mip.size.x * mip.size.y * texelData.texelSize;
      }
    }
    assert(currOffset == texelData.texels.size());
    return texelData;
  }



  static ImageTexelData LoadTexelDataFromGli(const gli::texture &texture)
  {
    glm::uvec3 size = { texture.extent().x, texture.extent().y, texture.extent().z };
    ImageTexelData texelData;
    size_t mipsCount = texture.max_level() + 1;
    texelData.layersCount = texture.max_face() + 1;
    switch (texture.format())
    {
      case gli::format::FORMAT_RGBA8_UINT_PACK8:
      case gli::format::FORMAT_RGBA8_SRGB_PACK8:
      {
        texelData.format = vk::Format::eR8G8B8A8Srgb;
        texelData.texelSize = 4 * sizeof(uint8_t);
      }break;
      case gli::format::FORMAT_RGB8_UINT_PACK8:
      {
        texelData.format = vk::Format::eR8G8B8Srgb;
        texelData.texelSize = 3 * sizeof(uint8_t);
      }break;
      case gli::format::FORMAT_RGBA8_UNORM_PACK8:
      {
        texelData.format = vk::Format::eR8G8B8A8Unorm;
        texelData.texelSize = 4 * sizeof(uint8_t);
      }break;
      case gli::format::FORMAT_RGBA32_SFLOAT_PACK32:
      {
        texelData.format = vk::Format::eR32G32B32A32Sfloat;
        texelData.texelSize = 4 * sizeof(float);
      }break;
      case gli::format::FORMAT_RGBA16_SFLOAT_PACK16:
      {
        texelData.format = vk::Format::eR16G16B16A16Sfloat;
        texelData.texelSize = 2 * sizeof(float);
      }break;
      default:
      {
        assert(0);
      }break;
    }
    texelData.mips.resize(mipsCount);
    texelData.baseSize = size;
    texelData.texels.resize(/*size.x * size.y * texelData.layersCount * texelData.texelSize*/texture.size());

    size_t currOffset = 0;
    for (size_t mipLevel = 0; mipLevel < mipsCount; mipLevel++)
    {
      auto &mip = texelData.mips[mipLevel];
      mip.layers.resize(texelData.layersCount);

      mip.size = { texture.extent(mipLevel).x, texture.extent(mipLevel).y, texture.extent(mipLevel).z };
      for (size_t faceIndex = 0; faceIndex < texelData.layersCount; faceIndex++)
      {
        auto &layer = mip.layers[faceIndex];
        layer.offset = currOffset;

        uint8_t *dstTexelsPtr = texelData.texels.data() + currOffset;
        uint8_t *srcTexelsPtr = (uint8_t*)texture.data(0, faceIndex, mipLevel);

        memcpy(dstTexelsPtr, srcTexelsPtr, mip.size.x * mip.size.y * mip.size.z * texelData.texelSize);
        /*for (size_t z = 0; z < size.z; z++)
        {
          for (size_t y = 0; y < size.y; y++)
          {
            for (size_t x = 0; x < size.x; x++)
            {
              size_t localOffset = (x + y * mip.size.x + z * mip.size.x * mip.size.y) * texelData.texelSize;

              struct RGB32f
              {
                float r;
                float g;
                float b;
              };

              RGB32f *srcTexelRgb32f = (RGB32f*)(srcTexelsPtr + localOffset);
              RGB32f *dstTexelRgb32f = (RGB32f*)(dstTexelsPtr + localOffset);
              *dstTexelRgb32f = *srcTexelRgb32f;
            }
          }
        }*/
        currOffset += mip.size.x * mip.size.y * texelData.texelSize;
      }
    }
    assert(texelData.texels.size() == currOffset);
    return texelData;
  }
  static ImageTexelData LoadKtxFromFile(std::string filename)
  {
    gli::texture texture = gli::load(filename);
    if (!texture.empty())
      return LoadTexelDataFromGli(texture);
    else
      return ImageTexelData();
  }



  static gli::texture LoadTexelDataToGli(ImageTexelData texelData)
  {
    gli::texture::format_type format;
    switch (texelData.format)
    {
      case vk::Format::eR8G8B8A8Unorm:
      {
        format = gli::format::FORMAT_RGBA8_UNORM_PACK8;
      }break;
      case vk::Format::eR8G8B8A8Srgb:
      {
        format = gli::format::FORMAT_RGBA8_SRGB_PACK8;
      }break;
      case vk::Format::eR32G32B32A32Sfloat:
      {
        format = gli::format::FORMAT_RGBA32_SFLOAT_PACK32;
      }break;
      case vk::Format::eR16G16B16A16Sfloat:
      {
        format = gli::format::FORMAT_RGBA16_SFLOAT_PACK16;
      }break;
      default:
      {
        assert(0);
      }break;
    }
    //gli::target::target_type
    gli::texture texture(gli::target::TARGET_2D, format, gli::extent3d(texelData.baseSize.x, texelData.baseSize.y, texelData.baseSize.z), texelData.layersCount, 1, texelData.mips.size());

    for (size_t mipLevel = 0; mipLevel < texelData.mips.size(); mipLevel++)
    {
      auto &mip = texelData.mips[mipLevel];
      mip.size = { texture.extent(mipLevel).x, texture.extent(mipLevel).y, texture.extent(mipLevel).z };
      for (size_t layerIndex = 0; layerIndex < texelData.layersCount; layerIndex++)
      {
        auto &layer = mip.layers[layerIndex];
        size_t currOffset = layer.offset;

        uint8_t *srcTexelsPtr = texelData.texels.data() + currOffset;
        uint8_t *dstTexelsPtr = (uint8_t*)texture.data(layerIndex, 0, mipLevel);

        memcpy(dstTexelsPtr, srcTexelsPtr, mip.size.x * mip.size.y * mip.size.z * texelData.texelSize);
      }
    }
    return texture;
  }

  static void SaveKtxToFile(const ImageTexelData& data, std::string filename)
  {
    auto gliData = legit::LoadTexelDataToGli(data);
    gli::save(gliData, filename);
  }


  static size_t GetFormatSize(vk::Format format)
  {
    switch(format)
    {
      case vk::Format::eR8G8B8A8Unorm:
        return 4;
      break;
      case vk::Format::eR32G32B32A32Sfloat:
        return 4 * sizeof(float);
      break;
      case vk::Format::eR16G16B16A16Sfloat:
        return 2 * sizeof(float);
      break;
    }
    assert(0);
    return -1;
  }
  static legit::ImageTexelData CreateSimpleImageTexelData(glm::uint8 *pixels, int width, int height, vk::Format format = vk::Format::eR8G8B8A8Unorm)
  {
    legit::ImageTexelData texelData;
    texelData.baseSize = glm::uvec3(width, height, 1);
    texelData.format = format;
    texelData.layersCount = 1;
    texelData.mips.resize(1);
    texelData.mips[0].size = texelData.baseSize;
    texelData.mips[0].layers.resize(texelData.layersCount);
    texelData.mips[0].layers[0].offset = 0;
    texelData.texelSize = GetFormatSize(format);
    texelData.texels.resize(width * height * texelData.texelSize);
    size_t totalSize = width * height * texelData.texelSize;
    memcpy(texelData.texels.data(), pixels, totalSize);
    return texelData;
  }



  static void AddTransitionBarrier(legit::ImageData *imageData, legit::ImageUsageTypes srcUsageType, legit::ImageUsageTypes dstUsageType, vk::CommandBuffer commandBuffer)
  {
    auto srcImageAccessPattern = GetSrcImageAccessPattern(srcUsageType);
    auto dstImageAccessPattern = GetDstImageAccessPattern(dstUsageType);

    auto range = vk::ImageSubresourceRange()
      .setAspectMask(imageData->GetAspectFlags())
      .setBaseArrayLayer(0)
      .setLayerCount(imageData->GetArrayLayersCount())
      .setBaseMipLevel(0)
      .setLevelCount(imageData->GetMipsCount());

    auto imageBarrier = vk::ImageMemoryBarrier()
      .setSrcAccessMask(srcImageAccessPattern.accessMask)
      .setOldLayout(srcImageAccessPattern.layout)
      .setDstAccessMask(dstImageAccessPattern.accessMask)
      .setNewLayout(dstImageAccessPattern.layout)
      .setSubresourceRange(range)
      .setImage(imageData->GetHandle());

    imageBarrier
      .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
      .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED);

    commandBuffer.pipelineBarrier(srcImageAccessPattern.stage, dstImageAccessPattern.stage, vk::DependencyFlags(), {}, {}, { imageBarrier });
  }

  static void LoadTexelData(legit::Core *core, const ImageTexelData *texelData, legit::ImageData *dstImageData, legit::ImageUsageTypes dstUsageType = legit::ImageUsageTypes::GraphicsShaderRead)
  {
    auto stagingBuffer = std::unique_ptr<legit::Buffer>(new legit::Buffer(core->GetPhysicalDevice(), core->GetLogicalDevice(), texelData->texels.size(), vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent));

    void *bufferData = stagingBuffer->Map();
    memcpy(bufferData, texelData->texels.data(), texelData->texels.size());
    stagingBuffer->Unmap();

    std::vector<vk::BufferImageCopy> copyRegions;
    for (uint32_t mipLevel = 0; mipLevel < uint32_t(texelData->mips.size()); mipLevel++)
    {
      const auto &mip = texelData->mips[mipLevel];

      for (uint32_t arrayLayer = 0; arrayLayer < uint32_t(mip.layers.size()); arrayLayer++)
      {
        const auto &layer = mip.layers[arrayLayer];
        auto imageSubresource = vk::ImageSubresourceLayers()
          .setAspectMask(vk::ImageAspectFlagBits::eColor)
          .setMipLevel(mipLevel)
          .setBaseArrayLayer(arrayLayer)
          .setLayerCount(1);

        auto copyRegion = vk::BufferImageCopy()
          .setBufferOffset(layer.offset)
          .setBufferRowLength(0)
          .setBufferImageHeight(0)
          .setImageSubresource(imageSubresource)
          .setImageOffset(vk::Offset3D(0, 0, 0))
          .setImageExtent(vk::Extent3D(mip.size.x, mip.size.y, mip.size.z));

        copyRegions.push_back(copyRegion);
      }
    }

    legit::ExecuteOnceQueue transferQueue(core);
    auto transferCommandBuffer = transferQueue.BeginCommandBuffer();
    {
      AddTransitionBarrier(dstImageData, legit::ImageUsageTypes::None, legit::ImageUsageTypes::TransferDst, transferCommandBuffer);
      transferCommandBuffer.copyBufferToImage(stagingBuffer->GetHandle(), dstImageData->GetHandle(), vk::ImageLayout::eTransferDstOptimal, copyRegions);
      AddTransitionBarrier(dstImageData, legit::ImageUsageTypes::TransferDst, dstUsageType, transferCommandBuffer);
    }
    transferQueue.EndCommandBuffer();
  }
}