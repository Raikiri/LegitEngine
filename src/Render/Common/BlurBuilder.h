#pragma once

class BlurBuilder
{
public:
  BlurBuilder(legit::Core *_core)
  {
    this->core = _core;

    imageSpaceSampler.reset(new legit::Sampler(core->GetLogicalDevice(), vk::SamplerAddressMode::eClampToEdge, vk::Filter::eNearest, vk::SamplerMipmapMode::eNearest));
    ReloadShaders();
  }

  void ApplyBlur(legit::RenderGraph *renderGraph, legit::ShaderMemoryPool *memoryPool, legit::RenderGraph::ImageViewProxyId srcProxyId, legit::RenderGraph::ImageViewProxyId dstProxyId, int radius)
  {
    glm::uvec2 viewportSize = renderGraph->GetMipSize(srcProxyId, 0);
    assert(viewportSize == renderGraph->GetMipSize(dstProxyId, 0));
    vk::Extent2D layerSize(viewportSize.x, viewportSize.y);

    renderGraph->AddPass(legit::RenderGraph::RenderPassDesc()
      .SetColorAttachments({dstProxyId})
      .SetInputImages({srcProxyId})
      .SetRenderAreaExtent(layerSize)
      .SetProfilerInfo(legit::Colors::wisteria, "BlurPass")
      .SetRecordFunc([this, memoryPool, srcProxyId, viewportSize, radius](legit::RenderGraph::RenderPassContext passContext)
    {
      auto pipeineInfo = this->core->GetPipelineCache()->BindGraphicsPipeline(passContext.GetCommandBuffer(), passContext.GetRenderPass()->GetHandle(), legit::DepthSettings::Disabled(), { legit::BlendSettings::Opaque() }, legit::VertexDeclaration(), vk::PrimitiveTopology::eTriangleFan, blurLayerBuilder.shaderProgram.get());
      {
        const legit::DescriptorSetLayoutKey *shaderDataSetInfo = blurLayerBuilder.fragmentShader->GetSetInfo(ShaderDataSetIndex);
        auto shaderUniformData = memoryPool->BeginSet(shaderDataSetInfo);
        {
          auto shaderDataBuffer = memoryPool->GetUniformBufferData<BlurLayerBuilder::ShaderDataBuffer>("BlurLayerBuilderData");
          shaderDataBuffer->size = glm::ivec4(viewportSize.x, viewportSize.y, 0, 0);
          shaderDataBuffer->radius = radius;
        }
        memoryPool->EndSet();

        std::vector<legit::ImageSamplerBinding> imageSamplerBindings;
        imageSamplerBindings.push_back(shaderDataSetInfo->MakeImageSamplerBinding("srcSampler", passContext.GetImageView(srcProxyId), imageSpaceSampler.get()));

        auto shaderDataSet = this->core->GetDescriptorSetCache()->GetDescriptorSet(*shaderDataSetInfo, shaderUniformData.uniformBufferBindings, {}, imageSamplerBindings);
        passContext.GetCommandBuffer().bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeineInfo.pipelineLayout, ShaderDataSetIndex, { shaderDataSet }, { shaderUniformData.dynamicOffset });
        passContext.GetCommandBuffer().draw(4, 1, 0, 0);
      }
    }));
  }

  void ReloadShaders()
  {
    blurLayerBuilder.vertexShader.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/Common/screenspaceQuad.vert.spv"));
    blurLayerBuilder.fragmentShader.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/Common/blurLayerBuilder.frag.spv"));
    blurLayerBuilder.shaderProgram.reset(new legit::ShaderProgram(blurLayerBuilder.vertexShader.get(), blurLayerBuilder.fragmentShader.get()));
  }
private:


  const static uint32_t ShaderDataSetIndex = 0;
  const static uint32_t DrawCallDataSetIndex = 1;

  struct BlurLayerBuilder
  {
#pragma pack(push, 1)
    struct ShaderDataBuffer
    {
      glm::ivec4 size;
      int radius;
    };
#pragma pack(pop)

    std::unique_ptr<legit::Shader> vertexShader;
    std::unique_ptr<legit::Shader> fragmentShader;
    std::unique_ptr<legit::ShaderProgram> shaderProgram;
  } blurLayerBuilder;

  vk::Extent2D viewportSize;

  std::unique_ptr<legit::Sampler> imageSpaceSampler;

  legit::Core *core;
};