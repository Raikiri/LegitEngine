namespace legit
{
  enum struct QueueFamilyTypes
  {
    Graphics,
    Transfer,
    Compute,
    Present,
    Undefined
  };
  struct ImageAccessPattern
  {
    vk::PipelineStageFlags stage;
    vk::AccessFlags accessMask;
    vk::ImageLayout layout;
    QueueFamilyTypes queueFamilyType;
  };
  struct ImageSubresourceBarrier
  {
    ImageAccessPattern accessPattern;
    ImageAccessPattern dstAccessPattern;
  };

  enum struct ImageUsageTypes
  {
    GraphicsShaderRead,
    GraphicsShaderReadWrite,
    ComputeShaderRead,
    ComputeShaderReadWrite,
    TransferDst,
    TransferSrc,
    ColorAttachment,
    DepthAttachment,
    Present,
    None,
    Unknown //means it can be anything
  };

  ImageAccessPattern GetSrcImageAccessPattern(ImageUsageTypes usageType)
  {
    ImageAccessPattern accessPattern;
    switch (usageType)
    {
      case ImageUsageTypes::GraphicsShaderRead:
      {
        accessPattern.stage = vk::PipelineStageFlagBits::eVertexShader;
        accessPattern.accessMask = vk::AccessFlags();
        accessPattern.layout = vk::ImageLayout::eShaderReadOnlyOptimal;
        accessPattern.queueFamilyType = QueueFamilyTypes::Graphics;
      }break;
      case ImageUsageTypes::GraphicsShaderReadWrite:
      {
        accessPattern.stage = vk::PipelineStageFlagBits::eVertexShader | vk::PipelineStageFlagBits::eFragmentShader;
        accessPattern.accessMask = vk::AccessFlagBits::eShaderWrite;
        accessPattern.layout = vk::ImageLayout::eGeneral;
        accessPattern.queueFamilyType = QueueFamilyTypes::Graphics;
      }break;
      case ImageUsageTypes::ComputeShaderRead:
      {
        accessPattern.stage = vk::PipelineStageFlagBits::eComputeShader;
        accessPattern.accessMask = vk::AccessFlags();
        accessPattern.layout = vk::ImageLayout::eShaderReadOnlyOptimal;
        accessPattern.queueFamilyType = QueueFamilyTypes::Compute;
      }break;
      case ImageUsageTypes::ComputeShaderReadWrite:
      {
        accessPattern.stage = vk::PipelineStageFlagBits::eComputeShader;
        accessPattern.accessMask = vk::AccessFlagBits::eShaderWrite;
        accessPattern.layout = vk::ImageLayout::eGeneral;
        accessPattern.queueFamilyType = QueueFamilyTypes::Compute;
      }break;
      case ImageUsageTypes::TransferDst:
      {
        accessPattern.stage = vk::PipelineStageFlagBits::eTransfer;
        accessPattern.accessMask = vk::AccessFlagBits::eTransferWrite;
        accessPattern.layout = vk::ImageLayout::eTransferDstOptimal;
        accessPattern.queueFamilyType = QueueFamilyTypes::Transfer;
      }break;
      case ImageUsageTypes::TransferSrc:
      {
        accessPattern.stage = vk::PipelineStageFlagBits::eTransfer;
        accessPattern.accessMask = vk::AccessFlags();
        accessPattern.layout = vk::ImageLayout::eTransferSrcOptimal;
        accessPattern.queueFamilyType = QueueFamilyTypes::Transfer;
      }break;
      case ImageUsageTypes::ColorAttachment:
      {
        accessPattern.stage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        accessPattern.accessMask = vk::AccessFlagBits::eColorAttachmentWrite;
        accessPattern.layout = vk::ImageLayout::eColorAttachmentOptimal;
        accessPattern.queueFamilyType = QueueFamilyTypes::Graphics;
      }break;
      case ImageUsageTypes::DepthAttachment:
      {
        accessPattern.stage = vk::PipelineStageFlagBits::eLateFragmentTests;
        accessPattern.accessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
        accessPattern.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
        accessPattern.queueFamilyType = QueueFamilyTypes::Graphics;
      }break;
      case ImageUsageTypes::Present:
      {
        accessPattern.stage = vk::PipelineStageFlagBits::eBottomOfPipe;
        accessPattern.accessMask = vk::AccessFlags();
        accessPattern.layout = vk::ImageLayout::ePresentSrcKHR;
        accessPattern.queueFamilyType = QueueFamilyTypes::Present;
      }break;
      case ImageUsageTypes::None:
      {
        accessPattern.stage = vk::PipelineStageFlagBits::eTopOfPipe;
        accessPattern.accessMask = vk::AccessFlags();
        accessPattern.layout = vk::ImageLayout::eUndefined;
        accessPattern.queueFamilyType = QueueFamilyTypes::Undefined;
      }break;
      case ImageUsageTypes::Unknown:
      {
        accessPattern.stage = vk::PipelineStageFlagBits::eBottomOfPipe;
        accessPattern.accessMask = vk::AccessFlags();
        accessPattern.layout = vk::ImageLayout::eUndefined;
        accessPattern.queueFamilyType = QueueFamilyTypes::Undefined;
      }break;
    };
    return accessPattern;
  }

  ImageAccessPattern GetDstImageAccessPattern(ImageUsageTypes usageType)
  {
    ImageAccessPattern accessPattern;
    switch (usageType)
    {
      case ImageUsageTypes::GraphicsShaderRead:
      {
        accessPattern.stage = vk::PipelineStageFlagBits::eVertexShader;
        accessPattern.accessMask = vk::AccessFlagBits::eShaderRead;
        accessPattern.layout = vk::ImageLayout::eShaderReadOnlyOptimal;
        accessPattern.queueFamilyType = QueueFamilyTypes::Graphics;
      }break;
      case ImageUsageTypes::GraphicsShaderReadWrite:
      {
        accessPattern.stage = vk::PipelineStageFlagBits::eVertexShader | vk::PipelineStageFlagBits::eFragmentShader;
        accessPattern.accessMask = vk::AccessFlagBits::eShaderWrite;
        accessPattern.layout = vk::ImageLayout::eGeneral;
        accessPattern.queueFamilyType = QueueFamilyTypes::Graphics;
      }break;
      case ImageUsageTypes::ComputeShaderRead:
      {
        accessPattern.stage = vk::PipelineStageFlagBits::eComputeShader;
        accessPattern.accessMask = vk::AccessFlagBits::eShaderRead;
        accessPattern.layout = vk::ImageLayout::eShaderReadOnlyOptimal;
        accessPattern.queueFamilyType = QueueFamilyTypes::Compute;
      }break;
      case ImageUsageTypes::ComputeShaderReadWrite:
      {
        accessPattern.stage = vk::PipelineStageFlagBits::eComputeShader;
        accessPattern.accessMask = vk::AccessFlagBits::eShaderWrite | vk::AccessFlagBits::eShaderRead;
        accessPattern.layout = vk::ImageLayout::eGeneral;
        accessPattern.queueFamilyType = QueueFamilyTypes::Compute;
      }break;
      case ImageUsageTypes::TransferDst:
      {
        accessPattern.stage = vk::PipelineStageFlagBits::eTransfer;
        accessPattern.accessMask = vk::AccessFlagBits::eTransferWrite;
        accessPattern.layout = vk::ImageLayout::eTransferDstOptimal;
        accessPattern.queueFamilyType = QueueFamilyTypes::Transfer;
      }break;
      case ImageUsageTypes::TransferSrc:
      {
        accessPattern.stage = vk::PipelineStageFlagBits::eTransfer;
        accessPattern.accessMask = vk::AccessFlagBits::eTransferRead;
        accessPattern.layout = vk::ImageLayout::eTransferSrcOptimal;
        accessPattern.queueFamilyType = QueueFamilyTypes::Transfer;
      }break;
      case ImageUsageTypes::ColorAttachment:
      {
        accessPattern.stage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        accessPattern.accessMask = vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
        accessPattern.layout = vk::ImageLayout::eColorAttachmentOptimal;
        accessPattern.queueFamilyType = QueueFamilyTypes::Graphics;
      }break;
      case ImageUsageTypes::DepthAttachment:
      {
        accessPattern.stage = vk::PipelineStageFlagBits::eLateFragmentTests | vk::PipelineStageFlagBits::eEarlyFragmentTests;
        accessPattern.accessMask = vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite;
        accessPattern.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
        accessPattern.queueFamilyType = QueueFamilyTypes::Graphics;
      }break;
      case ImageUsageTypes::Present:
      {
        accessPattern.stage = vk::PipelineStageFlagBits::eBottomOfPipe;
        accessPattern.accessMask = vk::AccessFlags();
        accessPattern.layout = vk::ImageLayout::ePresentSrcKHR;
        accessPattern.queueFamilyType = QueueFamilyTypes::Present;
      }break;
      case ImageUsageTypes::None:
      {
        accessPattern.stage = vk::PipelineStageFlagBits::eBottomOfPipe;
        accessPattern.accessMask = vk::AccessFlags();
        accessPattern.layout = vk::ImageLayout::eUndefined;
        accessPattern.queueFamilyType = QueueFamilyTypes::Undefined;
      }break;
      case ImageUsageTypes::Unknown:
      {
        assert(0);
        accessPattern.stage = vk::PipelineStageFlagBits::eTopOfPipe;
        accessPattern.accessMask = vk::AccessFlags();
        accessPattern.layout = vk::ImageLayout::eUndefined;
        accessPattern.queueFamilyType = QueueFamilyTypes::Undefined;
      }break;
    };
    return accessPattern;
  }

  bool IsImageBarrierNeeded(ImageUsageTypes srcUsageType, ImageUsageTypes dstUsageType)
  {
    if (srcUsageType == ImageUsageTypes::GraphicsShaderRead && dstUsageType == ImageUsageTypes::GraphicsShaderRead)
      return false;
    return true;
  }

  struct BufferAccessPattern
  {
    vk::PipelineStageFlags stage;
    vk::AccessFlags accessMask;
    QueueFamilyTypes queueFamilyType;
  };
  struct BufferBarrier
  {
    ImageAccessPattern srcAccessPattern;
    ImageAccessPattern dstAccessPattern;
  };

  enum struct BufferUsageTypes
  {
    GraphicsShaderReadWrite,
    ComputeShaderReadWrite,
    TransferDst,
    TransferSrc,
    None,
    Unknown
  };

  BufferAccessPattern GetSrcBufferAccessPattern(BufferUsageTypes usageType)
  {
    BufferAccessPattern accessPattern;
    switch (usageType)
    {
      case BufferUsageTypes::GraphicsShaderReadWrite:
      {
        accessPattern.stage = vk::PipelineStageFlagBits::eVertexShader | vk::PipelineStageFlagBits::eFragmentShader;
        accessPattern.accessMask = vk::AccessFlagBits::eShaderWrite;
        accessPattern.queueFamilyType = QueueFamilyTypes::Graphics;
      }break;
      case BufferUsageTypes::ComputeShaderReadWrite:
      {
        accessPattern.stage = vk::PipelineStageFlagBits::eComputeShader;
        accessPattern.accessMask = vk::AccessFlagBits::eShaderWrite;
        accessPattern.queueFamilyType = QueueFamilyTypes::Compute;
      }break;
      case BufferUsageTypes::TransferDst:
      {
        accessPattern.stage = vk::PipelineStageFlagBits::eTransfer;
        accessPattern.accessMask = vk::AccessFlagBits::eTransferWrite;
        accessPattern.queueFamilyType = QueueFamilyTypes::Transfer;
      }break;
      case BufferUsageTypes::TransferSrc:
      {
        accessPattern.stage = vk::PipelineStageFlagBits::eTransfer;
        accessPattern.accessMask = vk::AccessFlags();
        accessPattern.queueFamilyType = QueueFamilyTypes::Transfer;
      }break;
      case BufferUsageTypes::None:
      {
        accessPattern.stage = vk::PipelineStageFlagBits::eTopOfPipe;
        accessPattern.accessMask = vk::AccessFlags();
        accessPattern.queueFamilyType = QueueFamilyTypes::Undefined;
      }break;
      case BufferUsageTypes::Unknown:
      {
        accessPattern.stage = vk::PipelineStageFlagBits::eBottomOfPipe;
        accessPattern.accessMask = vk::AccessFlags();
        accessPattern.queueFamilyType = QueueFamilyTypes::Undefined;
      }break;
    };
    return accessPattern;
  }

  BufferAccessPattern GetDstBufferAccessPattern(BufferUsageTypes usageType)
  {
    BufferAccessPattern accessPattern;
    switch (usageType)
    {
      case BufferUsageTypes::GraphicsShaderReadWrite:
      {
        accessPattern.stage = vk::PipelineStageFlagBits::eVertexShader | vk::PipelineStageFlagBits::eFragmentShader;
        accessPattern.accessMask = vk::AccessFlagBits::eShaderWrite | vk::AccessFlagBits::eShaderRead;
        accessPattern.queueFamilyType = QueueFamilyTypes::Graphics;
      }break;
      case BufferUsageTypes::ComputeShaderReadWrite:
      {
        accessPattern.stage = vk::PipelineStageFlagBits::eComputeShader;
        accessPattern.accessMask = vk::AccessFlagBits::eShaderWrite | vk::AccessFlagBits::eShaderRead;
        accessPattern.queueFamilyType = QueueFamilyTypes::Compute;
      }break;
      case BufferUsageTypes::TransferDst:
      {
        accessPattern.stage = vk::PipelineStageFlagBits::eTransfer;
        accessPattern.accessMask = vk::AccessFlagBits::eTransferWrite;
        accessPattern.queueFamilyType = QueueFamilyTypes::Transfer;
      }break;
      case BufferUsageTypes::TransferSrc:
      {
        accessPattern.stage = vk::PipelineStageFlagBits::eTransfer;
        accessPattern.accessMask = vk::AccessFlags();
        accessPattern.queueFamilyType = QueueFamilyTypes::Transfer;
      }break;
      case BufferUsageTypes::None:
      {
        accessPattern.stage = vk::PipelineStageFlagBits::eBottomOfPipe;
        accessPattern.accessMask = vk::AccessFlags();
        accessPattern.queueFamilyType = QueueFamilyTypes::Undefined;
      }break;
      case BufferUsageTypes::Unknown:
      {
        accessPattern.stage = vk::PipelineStageFlagBits::eBottomOfPipe;
        accessPattern.accessMask = vk::AccessFlags();
        accessPattern.queueFamilyType = QueueFamilyTypes::Undefined;
      }break;
    };
    return accessPattern;
  }

  bool IsBufferBarrierNeeded(BufferUsageTypes srcUsageType, BufferUsageTypes dstUsageType)
  {
    return true;
  }
}