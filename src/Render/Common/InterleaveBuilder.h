#pragma once

class InterleaveBuilder
{
public:
  InterleaveBuilder(legit::Core *_core)
  {
    this->core = _core;

    imageSpaceSampler.reset(new legit::Sampler(core->GetLogicalDevice(), vk::SamplerAddressMode::eClampToEdge, vk::Filter::eNearest, vk::SamplerMipmapMode::eNearest));
    ReloadShaders();
  }

  void Deinterleave(legit::RenderGraph *renderGraph, legit::ShaderMemoryPool *memoryPool, legit::RenderGraph::ImageViewProxyId interleavedProxyId, legit::RenderGraph::ImageViewProxyId deinterleavedProxyId, glm::uvec2 gridSize)
  {
    auto viewportSize = renderGraph->GetMipSize(interleavedProxyId, 0);

    vk::Extent2D viewportExtent(viewportSize.x, viewportSize.y);

    renderGraph->AddPass( legit::RenderGraph::RenderPassDesc()
      .SetColorAttachments({ {deinterleavedProxyId, vk::AttachmentLoadOp::eDontCare } })
      .SetInputImages({interleavedProxyId})
      .SetRenderAreaExtent(viewportExtent)
      .SetRecordFunc([this, memoryPool, interleavedProxyId, gridSize, viewportSize](legit::RenderGraph::RenderPassContext passContext)
    {
      auto pipeineInfo = this->core->GetPipelineCache()->BindGraphicsPipeline(passContext.GetCommandBuffer(), passContext.GetRenderPass()->GetHandle(), legit::DepthSettings::Disabled(), { legit::BlendSettings::Opaque() }, legit::VertexDeclaration(), vk::PrimitiveTopology::eTriangleFan, deinterleaveShader.program.get());
      {
        const legit::DescriptorSetLayoutKey *shaderDataSetInfo = deinterleaveShader.fragment->GetSetInfo(ShaderDataSetIndex);
        auto shaderDataUniformBindings = memoryPool->BeginSet(shaderDataSetInfo);
        {
          auto shaderDataBuffer = memoryPool->GetUniformBufferData<DeinterleaveShader::ShaderDataBuffer>("DeinterleaveData");
          shaderDataBuffer->gridSize = glm::ivec4(gridSize, 0.0f, 0.0f);
          shaderDataBuffer->viewportSize = glm::ivec4(viewportSize, 0.0f, 0.0f);
        }
        memoryPool->EndSet();

        std::vector<legit::ImageSamplerBinding> imageSamplerBindings;
        auto interleavedView = passContext.GetImageView(interleavedProxyId);
        imageSamplerBindings.push_back(shaderDataSetInfo->MakeImageSamplerBinding("interleavedSampler", interleavedView, imageSpaceSampler.get()));

        auto shaderDataSet = this->core->GetDescriptorSetCache()->GetDescriptorSet(*shaderDataSetInfo, shaderDataUniformBindings.uniformBufferBindings, {}, imageSamplerBindings);
        passContext.GetCommandBuffer().bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeineInfo.pipelineLayout, ShaderDataSetIndex, { shaderDataSet }, { shaderDataUniformBindings.dynamicOffset });
        passContext.GetCommandBuffer().draw(4, 1, 0, 0);
      }
    }));
  }

  void Interleave(legit::RenderGraph *renderGraph, legit::ShaderMemoryPool *memoryPool, legit::RenderGraph::ImageViewProxyId deinterleavedProxyId, legit::RenderGraph::ImageViewProxyId interleavedProxyId, glm::uvec2 gridSize)
  {
    auto viewportSize = renderGraph->GetMipSize(interleavedProxyId, 0);

    vk::Extent2D viewportExtent(viewportSize.x, viewportSize.y);

    renderGraph->AddPass(legit::RenderGraph::RenderPassDesc()
      .SetColorAttachments({ { interleavedProxyId, vk::AttachmentLoadOp::eDontCare } })
      .SetInputImages({ deinterleavedProxyId })
      .SetRenderAreaExtent(viewportExtent)
      .SetRecordFunc([this, memoryPool, deinterleavedProxyId, gridSize, viewportSize](legit::RenderGraph::RenderPassContext passContext)
    {
      auto pipeineInfo = this->core->GetPipelineCache()->BindGraphicsPipeline(passContext.GetCommandBuffer(), passContext.GetRenderPass()->GetHandle(), legit::DepthSettings::Disabled(), { legit::BlendSettings::Opaque() }, legit::VertexDeclaration(), vk::PrimitiveTopology::eTriangleFan, deinterleaveShader.program.get());
      {
        const legit::DescriptorSetLayoutKey *shaderDataSetInfo = deinterleaveShader.fragment->GetSetInfo(ShaderDataSetIndex);
        auto shaderDataUniformBindings = memoryPool->BeginSet(shaderDataSetInfo);
        {
          auto shaderDataBuffer = memoryPool->GetUniformBufferData<InterleaveShader::ShaderDataBuffer>("InterleaveData");
          shaderDataBuffer->gridSize = glm::ivec4(gridSize, 0.0f, 0.0f);
          shaderDataBuffer->viewportSize = glm::ivec4(viewportSize, 0.0f, 0.0f);
        }
        memoryPool->EndSet();

        std::vector<legit::ImageSamplerBinding> imageSamplerBindings;
        auto deinterleavedView = passContext.GetImageView(deinterleavedProxyId);
        imageSamplerBindings.push_back(shaderDataSetInfo->MakeImageSamplerBinding("deinterleavedSampler", deinterleavedView, imageSpaceSampler.get()));

        auto shaderDataSet = this->core->GetDescriptorSetCache()->GetDescriptorSet(*shaderDataSetInfo, shaderDataUniformBindings.uniformBufferBindings, {}, imageSamplerBindings);
        passContext.GetCommandBuffer().bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeineInfo.pipelineLayout, ShaderDataSetIndex, { shaderDataSet }, { shaderDataUniformBindings.dynamicOffset });
        passContext.GetCommandBuffer().draw(4, 1, 0, 0);
      }
    }));
  }

  void ReloadShaders()
  {
    deinterleaveShader.vertex.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/Common/screenspaceQuad.vert.spv"));
    deinterleaveShader.fragment.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/Common/deinterleave.frag.spv"));
    deinterleaveShader.program.reset(new legit::ShaderProgram(deinterleaveShader.vertex.get(), deinterleaveShader.fragment.get()));

    interleaveShader.vertex.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/Common/screenspaceQuad.vert.spv"));
    interleaveShader.fragment.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/Common/interleave.frag.spv"));
    interleaveShader.program.reset(new legit::ShaderProgram(interleaveShader.vertex.get(), interleaveShader.fragment.get()));
  }
private:


  const static uint32_t ShaderDataSetIndex = 0;
  const static uint32_t DrawCallDataSetIndex = 1;

  struct DeinterleaveShader
  {
#pragma pack(push, 1)
    struct ShaderDataBuffer
    {
      glm::ivec4 gridSize;
      glm::ivec4 viewportSize;
    };
#pragma pack(pop)

    std::unique_ptr<legit::Shader> vertex;
    std::unique_ptr<legit::Shader> fragment;
    std::unique_ptr<legit::ShaderProgram> program;
  } deinterleaveShader;

    struct InterleaveShader
  {
#pragma pack(push, 1)
    struct ShaderDataBuffer
    {
      glm::ivec4 gridSize;
      glm::ivec4 viewportSize;
    };
#pragma pack(pop)

    std::unique_ptr<legit::Shader> vertex;
    std::unique_ptr<legit::Shader> fragment;
    std::unique_ptr<legit::ShaderProgram> program;
  } interleaveShader;


  std::unique_ptr<legit::Sampler> imageSpaceSampler;

  legit::Core *core;
};