namespace legit
{
  class Core;
  class Pipeline
  {
  public:
    vk::Pipeline GetHandle()
    {
      return pipeline.get();
    }
  private:
    Pipeline(vk::Device logicalDevice, vk::ShaderModule vertexShader, vk::ShaderModule fragmentShader, vk::Extent2D viewportSize, vk::RenderPass renderPass)
    {
      auto vertexStageCreateInfo = vk::PipelineShaderStageCreateInfo()
        .setStage(vk::ShaderStageFlagBits::eVertex)
        .setModule(vertexShader)
        .setPName("main");

      auto fragmentStageCreateInfo = vk::PipelineShaderStageCreateInfo()
        .setStage(vk::ShaderStageFlagBits::eFragment)
        .setModule(fragmentShader)
        .setPName("main");

      vk::PipelineShaderStageCreateInfo shaderStageInfos[] = { vertexStageCreateInfo, fragmentStageCreateInfo };

      auto vertexInputInfo = vk::PipelineVertexInputStateCreateInfo()
        .setVertexBindingDescriptionCount(0)
        .setPVertexBindingDescriptions(nullptr)
        .setVertexAttributeDescriptionCount(0)
        .setPVertexAttributeDescriptions(nullptr);

      auto inputAssemblyInfo = vk::PipelineInputAssemblyStateCreateInfo()
        .setTopology(vk::PrimitiveTopology::eTriangleList)
        .setPrimitiveRestartEnable(false);

      auto viewport = vk::Viewport()
        .setWidth(float(viewportSize.width))
        .setHeight(float(viewportSize.height))
        .setMinDepth(0.0f)
        .setMaxDepth(1.0f);

      auto scissorRect = vk::Rect2D()
        .setExtent(viewportSize);

      auto viewportStateInfo = vk::PipelineViewportStateCreateInfo()
        .setViewportCount(1)
        .setPViewports(&viewport)
        .setScissorCount(1)
        .setPScissors(&scissorRect);

      auto rasterizationStateInfo = vk::PipelineRasterizationStateCreateInfo()
        .setDepthClampEnable(false)
        .setPolygonMode(vk::PolygonMode::eFill)
        .setLineWidth(1.0f)
        .setCullMode(vk::CullModeFlagBits::eBack)
        .setFrontFace(vk::FrontFace::eClockwise)
        .setDepthBiasEnable(false);

      auto multisampleStateInfo = vk::PipelineMultisampleStateCreateInfo()
        .setSampleShadingEnable(false)
        .setRasterizationSamples(vk::SampleCountFlagBits::e1);

      auto colorBlendAttachnment = vk::PipelineColorBlendAttachmentState()
        .setColorWriteMask(vk::ColorComponentFlagBits::eA | vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB)
        .setBlendEnable(false);

      auto colorBlendStateInfo = vk::PipelineColorBlendStateCreateInfo()
        .setLogicOpEnable(false)
        .setAttachmentCount(1)
        .setPAttachments(&colorBlendAttachnment);

      auto dynamicStateInfo = vk::PipelineDynamicStateCreateInfo();

      auto pipelineLayoutInfo = vk::PipelineLayoutCreateInfo()
        .setSetLayoutCount(0)
        .setPSetLayouts(nullptr)
        .setPushConstantRangeCount(0)
        .setPPushConstantRanges(nullptr);

      this->pipelineLayout = logicalDevice.createPipelineLayoutUnique(pipelineLayoutInfo);

      auto pipelineCreateInfo = vk::GraphicsPipelineCreateInfo()
        .setStageCount(2)
        .setPStages(shaderStageInfos)
        .setPVertexInputState(&vertexInputInfo)
        .setPInputAssemblyState(&inputAssemblyInfo)
        .setPRasterizationState(&rasterizationStateInfo)
        .setPViewportState(&viewportStateInfo)
        .setPMultisampleState(&multisampleStateInfo)
        .setPDepthStencilState(nullptr)
        .setPColorBlendState(&colorBlendStateInfo)
        .setPDynamicState(nullptr)
        .setLayout(pipelineLayout.get())
        .setRenderPass(renderPass)
        .setSubpass(0)
        .setBasePipelineHandle(nullptr) //use later
        .setBasePipelineIndex(-1);

      pipeline = logicalDevice.createGraphicsPipelineUnique(nullptr, pipelineCreateInfo);
    }
    vk::UniquePipelineLayout pipelineLayout;
    vk::UniquePipeline pipeline;
    friend class Core;
  };
}