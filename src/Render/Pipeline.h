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
    Pipeline(
      vk::Device logicalDevice, 
      vk::ShaderModule vertexShader, vk::ShaderModule fragmentShader, 
      const legit::VertexDeclaration &vertexDecl,
      vk::RenderPass renderPass)
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
        .setVertexBindingDescriptionCount(uint32_t(vertexDecl.GetBindingDescriptors().size()))
        .setPVertexBindingDescriptions(vertexDecl.GetBindingDescriptors().data())
        .setVertexAttributeDescriptionCount(uint32_t(vertexDecl.GetVertexAttributes().size()))
        .setPVertexAttributeDescriptions(vertexDecl.GetVertexAttributes().data());

      auto inputAssemblyInfo = vk::PipelineInputAssemblyStateCreateInfo()
        .setTopology(vk::PrimitiveTopology::eTriangleList)
        .setPrimitiveRestartEnable(false);

      auto rasterizationStateInfo = vk::PipelineRasterizationStateCreateInfo()
        .setDepthClampEnable(false)
        .setPolygonMode(vk::PolygonMode::eFill)
        .setLineWidth(1.0f)
        .setCullMode(vk::CullModeFlagBits::eNone)
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

      vk::DynamicState dynamicStates[] = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
      auto dynamicStateInfo = vk::PipelineDynamicStateCreateInfo()
        .setDynamicStateCount(2)
        .setPDynamicStates(dynamicStates);

      auto pipelineLayoutInfo = vk::PipelineLayoutCreateInfo()
        .setSetLayoutCount(0)
        .setPSetLayouts(nullptr)
        .setPushConstantRangeCount(0)
        .setPPushConstantRanges(nullptr);

      this->pipelineLayout = logicalDevice.createPipelineLayoutUnique(pipelineLayoutInfo);
      
      auto viewportState = vk::PipelineViewportStateCreateInfo()
        .setScissorCount(1)
        .setViewportCount(1);
      auto pipelineCreateInfo = vk::GraphicsPipelineCreateInfo()
        .setStageCount(2)
        .setPStages(shaderStageInfos)
        .setPVertexInputState(&vertexInputInfo)
        .setPInputAssemblyState(&inputAssemblyInfo)
        .setPRasterizationState(&rasterizationStateInfo)
        .setPViewportState(&viewportState)
        .setPMultisampleState(&multisampleStateInfo)
        .setPDepthStencilState(nullptr)
        .setPColorBlendState(&colorBlendStateInfo)
        .setPDynamicState(&dynamicStateInfo)
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