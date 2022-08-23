namespace legit
{
  //srcAccessPattern.stage = vk::PipelineStageFlagBits::eTransfer; //memory from this stage
  //srcAccessPattern.accessMask = vk::AccessFlagBits::eTransferWrite; //of this type
  //has to become available 
  //dstAccessPattern.stage = vk::PipelineStageFlagBits::eVertexInput; //in order to be made visible for this stage
  //dstAccessPattern.accessMask = vk::AccessFlags::eVertexAttributeRead; //for this memory type

  //before trying to read any dstAccessMask from dstStage(or later), make sure that writes to any srcAccessMask from srcStage (or earlier) have completed.

  // it doesn�t make sense to mask access bits that correspond to reads (e.g. eShaderRead) in the source access mask.
  // Reading from memory from different stages without a write issued in between is well-defined and requires no barrier

  //we can only consider 1 previous pass that used the same image because that pass could not execute unless memory of this texture was made available before it.
  //since the purpose of the left side of the barrier is to only make memory available, it should only be done if the previous pass writes this memory, otherwise it's already available

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

  static ImageAccessPattern GetSrcImageAccessPattern(ImageUsageTypes usageType)
  {
    ImageAccessPattern accessPattern;
    switch (usageType)
    {
      case ImageUsageTypes::GraphicsShaderRead:
      {
        accessPattern.stage = vk::PipelineStageFlagBits::eFragmentShader;
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

  static ImageAccessPattern GetDstImageAccessPattern(ImageUsageTypes usageType)
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
        accessPattern.accessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;
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

  static bool IsImageBarrierNeeded(ImageUsageTypes srcUsageType, ImageUsageTypes dstUsageType)
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
    VertexBuffer,
    GraphicsShaderReadWrite,
    ComputeShaderReadWrite,
    TransferDst,
    TransferSrc,
    None,
    Unknown
  };

  static BufferAccessPattern GetSrcBufferAccessPattern(BufferUsageTypes usageType)
  {
    BufferAccessPattern accessPattern;
    switch (usageType)
    {
      case BufferUsageTypes::VertexBuffer:
      {
        accessPattern.stage = vk::PipelineStageFlagBits::eVertexInput;
        accessPattern.accessMask = vk::AccessFlags();
        accessPattern.queueFamilyType = QueueFamilyTypes::Graphics;
      }break;
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

  static BufferAccessPattern GetDstBufferAccessPattern(BufferUsageTypes usageType)
  {
    BufferAccessPattern accessPattern;
    switch (usageType)
    {
      case BufferUsageTypes::VertexBuffer:
      {
        accessPattern.stage = vk::PipelineStageFlagBits::eVertexInput;
        accessPattern.accessMask = vk::AccessFlagBits::eVertexAttributeRead;
        accessPattern.queueFamilyType = QueueFamilyTypes::Graphics;
      }break;
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
        accessPattern.accessMask = vk::AccessFlagBits::eTransferRead;
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

  static bool IsBufferBarrierNeeded(BufferUsageTypes srcUsageType, BufferUsageTypes dstUsageType)
  {
    //could be smarter
    return true;
  }
}