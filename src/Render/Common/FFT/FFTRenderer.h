#pragma once
//#include "FFT.h"
const float pi = 3.141592f;
glm::int32 idot(glm::ivec3 a, glm::ivec3 b)
{
  return a.x * b.x + a.y * b.y + a.z * b.z;
}


class FFTRenderer
{
public:
  FFTRenderer(legit::Core *_core)
  {
    this->core = _core;

    ReloadShaders();
  }
public:
  struct BucketBuffers
  {
    legit::RenderGraph::BufferProxyId bucketsProxyId;
    legit::RenderGraph::BufferProxyId mipInfosProxyId;
    legit::RenderGraph::BufferProxyId pointsListProxyId;
    legit::RenderGraph::BufferProxyId blockPointsListProxyId;
    size_t totalBucketsCount;
  };

  void RecreateSwapchainResources(glm::uvec2 viewportSize, size_t framesInFlightCount, size_t maxMipsCount = std::numeric_limits<size_t>::max())
  {
  }

  void RecreateSceneResources(glm::ivec3 size)
  {
  }

  void FFT3d(legit::ShaderMemoryPool *memoryPool, legit::RenderGraph::ImageViewProxyId volumeProxy, glm::ivec3 size, bool isForward)
  {
    glm::ivec3 axes[] = {
      glm::ivec3(1, 0, 0),
      glm::ivec3(0, 1, 0),
      glm::ivec3(0, 0, 1) };
    for (size_t phase = 0; phase < 3; phase++)
    {
      PostProcessPass(memoryPool, volumeProxy, size, axes[phase], isForward);
      CooleyTukeyPass(memoryPool, volumeProxy, size, axes[phase], isForward);
    }
  }

  void PostProcessPass(
    legit::ShaderMemoryPool *memoryPool,
    legit::RenderGraph::ImageViewProxyId volumeProxy,
    glm::ivec3 size,
    glm::ivec3 transformAxis,
    bool isForward)
  {
    #pragma pack(push, 1)
    struct ShaderDataBuffer
    {
      glm::ivec4 sizePow;
      glm::ivec4 transformAxis;
      float ampMult;
    } shaderDataBuf;
    #pragma pack(pop)

    int transformSize = idot(size, transformAxis);

    glm::ivec3 sizePow = glm::ivec3(0);
    while ((glm::uint32(1) << sizePow.x) < glm::uint32(size.x)) sizePow.x++;
    while ((glm::uint32(1) << sizePow.y) < glm::uint32(size.y)) sizePow.y++;
    while ((glm::uint32(1) << sizePow.z) < glm::uint32(size.z)) sizePow.z++;

    shaderDataBuf.sizePow = glm::ivec4(sizePow, 0);
    shaderDataBuf.transformAxis = glm::vec4(transformAxis, 0);
    shaderDataBuf.ampMult = isForward ? 1.0f : (1.0f / float(transformSize));

    core->GetRenderGraph()->AddPass(legit::RenderGraph::ComputePassDesc()
      .SetStorageImages({volumeProxy })
      .SetProfilerInfo(legit::Colors::belizeHole, "PassPostProcess")
      .SetRecordFunc([this, memoryPool, size, shaderDataBuf, volumeProxy](legit::RenderGraph::PassContext passContext)
    {
      auto shader = postProcessShader.compute.get();
      auto pipeineInfo = this->core->GetPipelineCache()->BindComputePipeline(passContext.GetCommandBuffer(), shader);
      {
        const legit::DescriptorSetLayoutKey *shaderDataSetInfo = shader->GetSetInfo(ShaderDataSetIndex);
        auto shaderData = memoryPool->BeginSet(shaderDataSetInfo);
        {
          auto mappedShaderDataBuf = memoryPool->GetUniformBufferData<ShaderDataBuffer>("ShaderDataBuffer");
          *mappedShaderDataBuf = shaderDataBuf;
        }
        memoryPool->EndSet();

        std::vector<legit::StorageImageBinding> storageImageBindings;
        auto volumeView = passContext.GetImageView(volumeProxy);
        storageImageBindings.push_back(shaderDataSetInfo->MakeStorageImageBinding("dataImage", volumeView));

        auto shaderDataSetBindings = legit::DescriptorSetBindings()
          .SetUniformBufferBindings(shaderData.uniformBufferBindings)
          .SetStorageImageBindings(storageImageBindings);/*
          .SetImageSamplerBindings(imageSamplerBindings);*/
        auto shaderDataSet = this->core->GetDescriptorSetCache()->GetDescriptorSet(*shaderDataSetInfo, shaderDataSetBindings);

        passContext.GetCommandBuffer().bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipeineInfo.pipelineLayout, ShaderDataSetIndex, { shaderDataSet }, { shaderData.dynamicOffset });

        glm::uvec3 workGroupSize = shader->GetLocalSize();
        passContext.GetCommandBuffer().dispatch(
          uint32_t(size.x / workGroupSize.x),
          uint32_t(size.y / workGroupSize.y),
          uint32_t(size.z / workGroupSize.z));
      }
    }));
  }

  void CooleyTukeyPass(legit::ShaderMemoryPool *memoryPool, legit::RenderGraph::ImageViewProxyId volumeProxy, glm::ivec3 size, glm::ivec3 transformAxis, bool isForward)
  {
    #pragma pack(push, 1)
    struct ShaderDataBuffer
    {
      glm::ivec4 size;
      glm::ivec4 transformAxis;
      float stepPhase;
      int gap;
    } shaderDataBuf;
    #pragma pack(pop)

    int transformSize = idot(size, transformAxis);
    glm::ivec3 halfSize = size - (transformSize / 2) * transformAxis;
    float dirPhaseMult = isForward ? -1.0f : 1.0f;

    //for (int gap = transformSize; gap > 1; gap /= 2)
    for (int gap = 2; gap <= transformSize; gap *= 2)
    {
      shaderDataBuf.size = glm::ivec4(size, 0);
      shaderDataBuf.transformAxis = glm::ivec4(transformAxis, 0);
      shaderDataBuf.stepPhase = 2.0f * dirPhaseMult * pi / float(gap);
      shaderDataBuf.gap = gap;

      core->GetRenderGraph()->AddPass(legit::RenderGraph::ComputePassDesc()
        .SetStorageImages({ volumeProxy })
        .SetProfilerInfo(legit::Colors::pumpkin, "PassCooleyTukey")
        .SetRecordFunc([this, memoryPool, halfSize, shaderDataBuf, volumeProxy](legit::RenderGraph::PassContext passContext)
      {
        auto shader = cooleyTukeyShader.compute.get();
        auto pipeineInfo = this->core->GetPipelineCache()->BindComputePipeline(passContext.GetCommandBuffer(), shader);
        {
          const legit::DescriptorSetLayoutKey *shaderDataSetInfo = shader->GetSetInfo(ShaderDataSetIndex);
          auto shaderData = memoryPool->BeginSet(shaderDataSetInfo);
          {
            auto mappedShaderDataBuf = memoryPool->GetUniformBufferData<ShaderDataBuffer>("ShaderDataBuffer");
            *mappedShaderDataBuf = shaderDataBuf;
          }
          memoryPool->EndSet();

          std::vector<legit::StorageImageBinding> storageImageBindings;
          auto volumeView = passContext.GetImageView(volumeProxy);
          storageImageBindings.push_back(shaderDataSetInfo->MakeStorageImageBinding("dataImage", volumeView));

          auto shaderDataSetBindings = legit::DescriptorSetBindings()
            .SetUniformBufferBindings(shaderData.uniformBufferBindings)
            .SetStorageImageBindings(storageImageBindings);/*
            .SetImageSamplerBindings(imageSamplerBindings);*/
          auto shaderDataSet = this->core->GetDescriptorSetCache()->GetDescriptorSet(*shaderDataSetInfo, shaderDataSetBindings);

          passContext.GetCommandBuffer().bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipeineInfo.pipelineLayout, ShaderDataSetIndex, { shaderDataSet }, { shaderData.dynamicOffset });

          glm::uvec3 workGroupSize = shader->GetLocalSize();
          passContext.GetCommandBuffer().dispatch(
            uint32_t(halfSize.x / workGroupSize.x),
            uint32_t(halfSize.y / workGroupSize.y),
            uint32_t(halfSize.z / workGroupSize.z));
        }
      }));
    }
  }
  
  void ReloadShaders()
  {
    postProcessShader.compute.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/Common/FFT/postProcess.comp.spv"));
    cooleyTukeyShader.compute.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/Common/FFT/cooleyTukeyPost.comp.spv"));
  }
private:


  const static uint32_t ShaderDataSetIndex = 0;
  const static uint32_t DrawCallDataSetIndex = 1;
  struct PostProcessShader
  {
    std::unique_ptr<legit::Shader> compute;
  } postProcessShader;

  struct CooleyTuckeyShader
  {
    std::unique_ptr<legit::Shader> compute;
  } cooleyTukeyShader;

  legit::Core *core;
};

