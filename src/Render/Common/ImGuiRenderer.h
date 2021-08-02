class ImGuiRenderer
{
public:
  ImGuiRenderer(legit::Core *_core, GLFWwindow *window)
  {
    this->core = _core;

    imguiContext = ImGui::CreateContext();
    ImGuiIO& imGuiIO = ImGui::GetIO();
    imGuiIO.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
    imGuiIO.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;       // Enable Keyboard Controls
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
    imGuiIO.ConfigFlags |= ImGuiConfigFlags_DockingEnable;           // Enable Docking
    imGuiIO.ConfigFlags |= ImGuiConfigFlags_IsSRGB;
    imGuiIO.ConfigWindowsResizeFromEdges = true;
    imGuiIO.ConfigDockingTabBarOnSingleWindows = true;
    //imGuiIO.ConfigFlags |= ImGuiConfigFlags_;
    //imGuiIO.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    // TODO: Set optional io.ConfigFlags values, e.g. 'io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard' to enable keyboard controls.
    // TODO: Fill optional fields of the io structure later.
    // TODO: Load TTF/OTF fonts if you don't want to use the default font.

    SetupStyle();
    ImFontConfig config;
    config.OversampleH = 4;
    config.OversampleV = 4;
    //imGuiIO.Fonts->AddFontFromFileTTF("../data/Fonts/DroidSansMono.ttf", 18.0f, &config);
    imGuiIO.Fonts->AddFontFromFileTTF("../data/Fonts/Ruda-Bold.ttf", 15.0f, &config);

    LoadImguiFont();

    imageSpaceSampler.reset(new legit::Sampler(core->GetLogicalDevice(), vk::SamplerAddressMode::eClampToEdge, vk::Filter::eLinear, vk::SamplerMipmapMode::eNearest));
    ReloadShaders();

    glfwSetWindowUserPointer(window, &(this->inputState));
    InitKeymap();
    InitCallbacks(window);
  }
  ~ImGuiRenderer()
  {
    ImGui::DestroyContext(imguiContext);
  }

  void SetupStyle()
  {
    ImGui::GetStyle().FrameRounding = 4.0f;
    ImGui::GetStyle().GrabRounding = 4.0f;
    
    ImVec4* colors = ImGui::GetStyle().Colors;
    colors[ImGuiCol_Text] = ImVec4(0.95f, 0.96f, 0.98f, 1.00f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.36f, 0.42f, 0.47f, 1.00f);
    colors[ImGuiCol_WindowBg] = ImVec4(0.11f, 0.15f, 0.17f, 0.37f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.15f, 0.18f, 0.22f, 0.16f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.20f, 0.25f, 0.29f, 0.57f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.12f, 0.20f, 0.28f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.09f, 0.12f, 0.14f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.21f, 0.27f, 0.31f, 1.00f);
    colors[ImGuiCol_CheckMark] = ImVec4(0.28f, 0.56f, 1.00f, 1.00f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.28f, 0.56f, 1.00f, 1.00f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.55f, 0.73f, 1.00f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.20f, 0.25f, 0.29f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.28f, 0.56f, 1.00f, 1.00f);
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.02f, 0.02f, 0.02f, 0.39f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.18f, 0.22f, 0.25f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.09f, 0.21f, 0.31f, 1.00f);
    colors[ImGuiCol_Tab] = ImVec4(0.07f, 0.10f, 0.15f, 0.00f);
    colors[ImGuiCol_TabActive] = ImVec4(0.19f, 0.41f, 0.78f, 1.00f);
    colors[ImGuiCol_TabUnfocused] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.20f, 0.25f, 0.29f, 0.80f);
    colors[ImGuiCol_Header] = ImVec4(0.20f, 0.25f, 0.29f, 0.55f);
    colors[ImGuiCol_Border] = ImVec4(0.08f, 0.10f, 0.12f, 1.00f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.09f, 0.12f, 0.14f, 0.65f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.08f, 0.10f, 0.12f, 1.00f);
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.15f, 0.18f, 0.22f, 1.00f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.20f, 0.25f, 0.29f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.20f, 0.25f, 0.29f, 0.55f);
    colors[ImGuiCol_Separator] = ImVec4(0.20f, 0.25f, 0.29f, 1.00f);



    colors[ImGuiCol_WindowBg] = ImVec4(0.11f, 0.15f, 0.17f, 0.20f);





    auto *style = &ImGui::GetStyle();

    /*colors[ImGuiCol_Text] = ImVec4(1.000f, 1.000f, 1.000f, 1.000f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.500f, 0.500f, 0.500f, 1.000f);
    colors[ImGuiCol_WindowBg] = ImVec4(0.180f, 0.180f, 0.180f, 1.000f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.280f, 0.280f, 0.280f, 0.000f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.313f, 0.313f, 0.313f, 1.000f);
    colors[ImGuiCol_Border] = ImVec4(0.266f, 0.266f, 0.266f, 1.000f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.000f, 0.000f, 0.000f, 0.000f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.160f, 0.160f, 0.160f, 1.000f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.200f, 0.200f, 0.200f, 1.000f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.280f, 0.280f, 0.280f, 1.000f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.148f, 0.148f, 0.148f, 1.000f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.148f, 0.148f, 0.148f, 1.000f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.148f, 0.148f, 0.148f, 1.000f);
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.195f, 0.195f, 0.195f, 1.000f);
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.160f, 0.160f, 0.160f, 1.000f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.277f, 0.277f, 0.277f, 1.000f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.300f, 0.300f, 0.300f, 1.000f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(1.000f, 0.391f, 0.000f, 1.000f);
    colors[ImGuiCol_CheckMark] = ImVec4(1.000f, 1.000f, 1.000f, 1.000f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.391f, 0.391f, 0.391f, 1.000f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(1.000f, 0.391f, 0.000f, 1.000f);
    colors[ImGuiCol_Button] = ImVec4(1.000f, 1.000f, 1.000f, 0.000f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(1.000f, 1.000f, 1.000f, 0.156f);
    colors[ImGuiCol_ButtonActive] = ImVec4(1.000f, 1.000f, 1.000f, 0.391f);
    colors[ImGuiCol_Header] = ImVec4(0.313f, 0.313f, 0.313f, 1.000f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.469f, 0.469f, 0.469f, 1.000f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.469f, 0.469f, 0.469f, 1.000f);
    colors[ImGuiCol_Separator] = colors[ImGuiCol_Border];
    colors[ImGuiCol_SeparatorHovered] = ImVec4(0.391f, 0.391f, 0.391f, 1.000f);
    colors[ImGuiCol_SeparatorActive] = ImVec4(1.000f, 0.391f, 0.000f, 1.000f);
    colors[ImGuiCol_ResizeGrip] = ImVec4(1.000f, 1.000f, 1.000f, 0.250f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(1.000f, 1.000f, 1.000f, 0.670f);
    colors[ImGuiCol_ResizeGripActive] = ImVec4(1.000f, 0.391f, 0.000f, 1.000f);
    colors[ImGuiCol_Tab] = ImVec4(0.098f, 0.098f, 0.098f, 1.000f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.352f, 0.352f, 0.352f, 1.000f);
    colors[ImGuiCol_TabActive] = ImVec4(0.195f, 0.195f, 0.195f, 1.000f);
    colors[ImGuiCol_TabUnfocused] = ImVec4(0.098f, 0.098f, 0.098f, 1.000f);
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.195f, 0.195f, 0.195f, 1.000f);
    //colors[ImGuiCol_DockingPreview] = ImVec4(1.000f, 0.391f, 0.000f, 0.781f);
    //colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.180f, 0.180f, 0.180f, 1.000f);
    colors[ImGuiCol_PlotLines] = ImVec4(0.469f, 0.469f, 0.469f, 1.000f);
    colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.000f, 0.391f, 0.000f, 1.000f);
    colors[ImGuiCol_PlotHistogram] = ImVec4(0.586f, 0.586f, 0.586f, 1.000f);
    colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.000f, 0.391f, 0.000f, 1.000f);
    colors[ImGuiCol_TextSelectedBg] = ImVec4(1.000f, 1.000f, 1.000f, 0.156f);
    colors[ImGuiCol_DragDropTarget] = ImVec4(1.000f, 0.391f, 0.000f, 1.000f);
    colors[ImGuiCol_NavHighlight] = ImVec4(1.000f, 0.391f, 0.000f, 1.000f);
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.000f, 0.391f, 0.000f, 1.000f);
    colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.000f, 0.000f, 0.000f, 0.586f);
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.000f, 0.000f, 0.000f, 0.586f);*/

    /*style->WindowPadding = ImVec2(15, 15);
    style->WindowRounding = 5.0f;
    style->FramePadding = ImVec2(5, 5);
    style->FrameRounding = 4.0f;
    style->ItemSpacing = ImVec2(12, 8);
    style->ItemInnerSpacing = ImVec2(8, 6);
    style->IndentSpacing = 25.0f;
    style->ScrollbarSize = 15.0f;
    style->ScrollbarRounding = 9.0f;
    style->GrabMinSize = 5.0f;
    style->GrabRounding = 3.0f;

    style->Colors[ImGuiCol_Text] = ImVec4(0.80f, 0.80f, 0.83f, 1.00f);
    style->Colors[ImGuiCol_TextDisabled] = ImVec4(0.24f, 0.23f, 0.29f, 1.00f);
    style->Colors[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.05f, 0.07f, 1.00f);
    style->Colors[ImGuiCol_ChildWindowBg] = ImVec4(0.07f, 0.07f, 0.09f, 1.00f);
    style->Colors[ImGuiCol_PopupBg] = ImVec4(0.07f, 0.07f, 0.09f, 1.00f);
    style->Colors[ImGuiCol_Border] = ImVec4(0.80f, 0.80f, 0.83f, 0.88f);
    style->Colors[ImGuiCol_BorderShadow] = ImVec4(0.92f, 0.91f, 0.88f, 0.00f);
    style->Colors[ImGuiCol_FrameBg] = ImVec4(0.10f, 0.09f, 0.12f, 1.00f);
    style->Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.24f, 0.23f, 0.29f, 1.00f);
    style->Colors[ImGuiCol_FrameBgActive] = ImVec4(0.56f, 0.56f, 0.58f, 1.00f);
    style->Colors[ImGuiCol_TitleBg] = ImVec4(0.10f, 0.09f, 0.12f, 1.00f);
    style->Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(1.00f, 0.98f, 0.95f, 0.75f);
    style->Colors[ImGuiCol_TitleBgActive] = ImVec4(0.07f, 0.07f, 0.09f, 1.00f);
    style->Colors[ImGuiCol_MenuBarBg] = ImVec4(0.10f, 0.09f, 0.12f, 1.00f);
    style->Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.10f, 0.09f, 0.12f, 1.00f);
    style->Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.80f, 0.80f, 0.83f, 0.31f);
    style->Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.56f, 0.56f, 0.58f, 1.00f);
    style->Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.06f, 0.05f, 0.07f, 1.00f);
    //style->Colors[ImGuiCol_ComboBg] = ImVec4(0.19f, 0.18f, 0.21f, 1.00f);
    style->Colors[ImGuiCol_CheckMark] = ImVec4(0.80f, 0.80f, 0.83f, 0.31f);
    style->Colors[ImGuiCol_SliderGrab] = ImVec4(0.80f, 0.80f, 0.83f, 0.31f);
    style->Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.06f, 0.05f, 0.07f, 1.00f);
    style->Colors[ImGuiCol_Button] = ImVec4(0.10f, 0.09f, 0.12f, 1.00f);
    style->Colors[ImGuiCol_ButtonHovered] = ImVec4(0.24f, 0.23f, 0.29f, 1.00f);
    style->Colors[ImGuiCol_ButtonActive] = ImVec4(0.56f, 0.56f, 0.58f, 1.00f);
    style->Colors[ImGuiCol_Header] = ImVec4(0.10f, 0.09f, 0.12f, 1.00f);
    style->Colors[ImGuiCol_HeaderHovered] = ImVec4(0.56f, 0.56f, 0.58f, 1.00f);
    style->Colors[ImGuiCol_HeaderActive] = ImVec4(0.06f, 0.05f, 0.07f, 1.00f);
    //style->Colors[ImGuiCol_Column] = ImVec4(0.56f, 0.56f, 0.58f, 1.00f);
    //style->Colors[ImGuiCol_ColumnHovered] = ImVec4(0.24f, 0.23f, 0.29f, 1.00f);
    //style->Colors[ImGuiCol_ColumnActive] = ImVec4(0.56f, 0.56f, 0.58f, 1.00f);
    style->Colors[ImGuiCol_ResizeGrip] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    style->Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.56f, 0.56f, 0.58f, 1.00f);
    style->Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.06f, 0.05f, 0.07f, 1.00f);
    //style->Colors[ImGuiCol_CloseButton] = ImVec4(0.40f, 0.39f, 0.38f, 0.16f);
    //style->Colors[ImGuiCol_CloseButtonHovered] = ImVec4(0.40f, 0.39f, 0.38f, 0.39f);
    //style->Colors[ImGuiCol_CloseButtonActive] = ImVec4(0.40f, 0.39f, 0.38f, 1.00f);
    style->Colors[ImGuiCol_PlotLines] = ImVec4(0.40f, 0.39f, 0.38f, 0.63f);
    style->Colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.25f, 1.00f, 0.00f, 1.00f);
    style->Colors[ImGuiCol_PlotHistogram] = ImVec4(0.40f, 0.39f, 0.38f, 0.63f);
    style->Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.25f, 1.00f, 0.00f, 1.00f);
    style->Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.25f, 1.00f, 0.00f, 0.43f);
    style->Colors[ImGuiCol_ModalWindowDarkening] = ImVec4(1.00f, 0.98f, 0.95f, 0.73f);*/
  }

  void RecreateSwapchainResources(vk::Extent2D viewportExtent, size_t inFlightFramesCount)
  {
    this->viewportExtent = viewportExtent;
    frameResources.clear();

    frameResources.resize(inFlightFramesCount);

    for (size_t frameIndex = 0; frameIndex < inFlightFramesCount; frameIndex++)
    {
      frameResources[frameIndex].reset(new FrameResources(core, 150000, 150000));
    }
  }

  void RenderFrame(const legit::InFlightQueue::FrameInfo &frameInfo, GLFWwindow *window, ImDrawData* drawData)
  {
    auto frameResources = this->frameResources[frameInfo.frameIndex].get();
    assert(frameResources);

    UploadBuffers(frameResources->imGuiVertexBuffer.get(), frameResources->imGuiIndexBuffer.get(), drawData);
    auto vertexBuffer = frameResources->imGuiVertexBuffer->GetHandle();
    auto indexBuffer = frameResources->imGuiIndexBuffer->GetHandle();
    core->GetRenderGraph()->AddPass(
      legit::RenderGraph::RenderPassDesc()
        .SetColorAttachments({ frameInfo.swapchainImageViewProxyId }, vk::AttachmentLoadOp::eLoad)
        .SetRenderAreaExtent(viewportExtent)
        .SetProfilerInfo(legit::Colors::peterRiver, "ImGuiPass")
        .SetRecordFunc([this, frameInfo, drawData, vertexBuffer, indexBuffer](legit::RenderGraph::RenderPassContext passContext)
    {
      auto pipeineInfo = this->core->GetPipelineCache()->BindGraphicsPipeline(
        passContext.GetCommandBuffer(), 
        passContext.GetRenderPass()->GetHandle(), 
        legit::DepthSettings::Disabled(), 
        { legit::BlendSettings::AlphaBlend() }, 
        GetImGuiVertexDeclaration(), 
        vk::PrimitiveTopology::eTriangleList, 
        imGuiShader.program.get());
      {
        /*glm::vec2 tileSize(0.1f, 0.1f);
        glm::vec2 tilePadding(0.02f, 0.02f);

        glm::vec2 currMin = tilePadding;
        for (auto debugProxyId : debugProxies)
        {
          const legit::DescriptorSetLayoutKey *vertexDataSetInfo = debugRendererShader.vertex->GetSetInfo(VertexDataSetIndex);
          auto vertexData = memoryPool->BeginSet(vertexDataSetInfo);
          {
            auto quadDataBuffer = memoryPool->GetUniformBufferData<DebugRendererShader::QuadData>("QuadData");
            quadDataBuffer->minmax = glm::vec4(currMin.x, currMin.y, currMin.x + tileSize.x, currMin.y + tileSize.y);
          }
          memoryPool->EndSet();
          auto vertexDataSet = this->core->GetDescriptorSetCache()->GetDescriptorSet(*vertexDataSetInfo, vertexData.uniformBufferBindings, {}, {});

          const legit::DescriptorSetLayoutKey *fragmentDataSetInfo = debugRendererShader.fragment->GetSetInfo(FragmentDataSetIndex);
          std::vector<legit::ImageSamplerBinding> imageSamplerBindings;
          imageSamplerBindings.push_back(fragmentDataSetInfo->MakeImageSamplerBinding("srcSampler", passContext.GetImageView(debugProxyId), imageSpaceSampler.get()));

          auto fragmentDataSet = this->core->GetDescriptorSetCache()->GetDescriptorSet(*fragmentDataSetInfo, {}, {}, { imageSamplerBindings });

          //passContext.GetCommandBuffer().bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeineInfo.pipelineLayout, VertexDataSetIndex, { vertexDataSet, fragmentDataSet }, { vertexDataOffset });
          passContext.GetCommandBuffer().bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeineInfo.pipelineLayout, VertexDataSetIndex, { vertexDataSet }, { vertexData.dynamicOffset });
          passContext.GetCommandBuffer().bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeineInfo.pipelineLayout, FragmentDataSetIndex, { fragmentDataSet }, {});

          passContext.GetCommandBuffer().draw(4, 1, 0, 0);

          currMin.x += tileSize.x + tilePadding.x;
          if (currMin.x + tileSize.x > 1.0f)
          {
            currMin.x = tilePadding.x;
            currMin.y += tileSize.y + tilePadding.y;
          }
        }*/
        const legit::DescriptorSetLayoutKey *shaderDataSetInfo = imGuiShader.program->GetSetInfo(ShaderDataSetIndex);
        auto shaderData = frameInfo.memoryPool->BeginSet(shaderDataSetInfo);
        {
          auto shaderDataBuffer = frameInfo.memoryPool->GetUniformBufferData<ImGuiShader::ImGuiShaderData>("ImGuiShaderData");
          shaderDataBuffer->projMatrix = glm::ortho(0.0f, float(viewportExtent.width), 0.0f, float(viewportExtent.height));
        }
        frameInfo.memoryPool->EndSet();

        auto shaderDataSet = this->core->GetDescriptorSetCache()->GetDescriptorSet(*shaderDataSetInfo, shaderData.uniformBufferBindings, {}, {});

        const legit::DescriptorSetLayoutKey *drawCallSetInfo = imGuiShader.program->GetSetInfo(DrawCallDataSetIndex);
        
        uint32_t listIndexOffset = 0;
        uint32_t listVertexOffset = 0;
        for (int cmdListIndex = 0; cmdListIndex < drawData->CmdListsCount; cmdListIndex++)
        {
          const ImDrawList* cmdList = drawData->CmdLists[cmdListIndex];
          const ImDrawVert* vertexBufferData = cmdList->VtxBuffer.Data;  // vertex buffer generated by Dear ImGui
          const ImDrawIdx* indexBufferData = cmdList->IdxBuffer.Data;   // index buffer generated by Dear ImGui

          for (int cmdIndex = 0; cmdIndex < cmdList->CmdBuffer.Size; cmdIndex++)
          {
            const ImDrawCmd* drawCmd = &cmdList->CmdBuffer[cmdIndex];
            if (drawCmd->UserCallback)
            {
              drawCmd->UserCallback(cmdList, drawCmd);
            }
            else
            {
              // The texture for the draw call is specified by pcmd->TextureId.
              // The vast majority of draw calls will use the Dear ImGui texture atlas, which value you have set yourself during initialization.

              legit::ImageView *texImageView = (legit::ImageView *)drawCmd->TextureId;
              legit::ImageSamplerBinding texBinding = drawCallSetInfo->MakeImageSamplerBinding("tex", texImageView, imageSpaceSampler.get());

              auto drawCallSet = this->core->GetDescriptorSetCache()->GetDescriptorSet(*drawCallSetInfo, {}, {}, { texBinding });

              passContext.GetCommandBuffer().bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeineInfo.pipelineLayout, ShaderDataSetIndex,
                { shaderDataSet, drawCallSet },
                { shaderData.dynamicOffset });


              // We are using scissoring to clip some objects. All low-level graphics API should supports it.
              // - If your engine doesn't support scissoring yet, you may ignore this at first. You will get some small glitches
              //   (some elements visible outside their bounds) but you can fix that once everything else works!
              // - Clipping coordinates are provided in imGui coordinates space (from draw_data->DisplayPos to draw_data->DisplayPos + draw_data->DisplaySize)
              //   In a single viewport application, draw_data->DisplayPos will always be (0,0) and draw_data->DisplaySize will always be == io.DisplaySize.
              //   However, in the interest of supporting multi-viewport applications in the future (see 'viewport' branch on github),
              //   always subtract draw_data->DisplayPos from clipping bounds to convert them to your viewport space.
              // - Note that pcmd->ClipRect contains Min+Max bounds. Some graphics API may use Min+Max, other may use Min+Size (size being Max-Min)
              //ImVec2 pos = draw_data->DisplayPos;
              //MyEngineScissor((int)(pcmd->ClipRect.x - pos.x), (int)(pcmd->ClipRect.y - pos.y), (int)(pcmd->ClipRect.z - pos.x), (int)(pcmd->ClipRect.w - pos.y));

              // Render 'pcmd->ElemCount/3' indexed triangles.
              // By default the indices ImDrawIdx are 16-bits, you can change them to 32-bits in imconfig.h if your engine doesn't support 16-bits indices.
              //MyEngineDrawIndexedTriangles(pcmd->ElemCount, sizeof(ImDrawIdx) == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT, idx_buffer, vtx_buffer);

              passContext.GetCommandBuffer().bindVertexBuffers(0, { vertexBuffer }, { 0 });
              passContext.GetCommandBuffer().bindIndexBuffer(indexBuffer, 0, sizeof(ImDrawIdx) == 2 ? vk::IndexType::eUint16 : vk::IndexType::eUint32);
              passContext.GetCommandBuffer().drawIndexed(drawCmd->ElemCount, 1, listIndexOffset + drawCmd->IdxOffset, listVertexOffset + drawCmd->VtxOffset, 0);
            }
          }
          listIndexOffset += cmdList->IdxBuffer.Size;
          listVertexOffset += cmdList->VtxBuffer.Size;
        }
      }
    }));
  }

  void ProcessInput(GLFWwindow *window)
  {
    bool isWindowFocused = glfwGetWindowAttrib(window, GLFW_FOCUSED);

    auto &imguiIO = ImGui::GetIO();

    glm::f64vec2 mousePos;
    glfwGetCursorPos(window, &mousePos.x, &mousePos.y);

    imguiIO.MousePos = ImVec2(float(mousePos.x), float(mousePos.y));             // set the mouse position

    //imguiIO.MouseWheel = inputState.mouseWheel;
    //inputState.mouseWheel = 0.0f;

    // Setup time step
    double currTime = glfwGetTime();
    imguiIO.DeltaTime = inputState.lastUpdateTime > 0.0 ? (float)(currTime - inputState.lastUpdateTime) : (float)(1.0f / 60.0f);
    inputState.lastUpdateTime = currTime;

    imguiIO.KeyCtrl = imguiIO.KeysDown[GLFW_KEY_LEFT_CONTROL] || imguiIO.KeysDown[GLFW_KEY_RIGHT_CONTROL];
    imguiIO.KeyShift = imguiIO.KeysDown[GLFW_KEY_LEFT_SHIFT] || imguiIO.KeysDown[GLFW_KEY_RIGHT_SHIFT];
    imguiIO.KeyAlt = imguiIO.KeysDown[GLFW_KEY_LEFT_ALT] || imguiIO.KeysDown[GLFW_KEY_RIGHT_ALT];
    imguiIO.KeySuper = imguiIO.KeysDown[GLFW_KEY_LEFT_SUPER] || imguiIO.KeysDown[GLFW_KEY_RIGHT_SUPER];
    /*for (int i = 0; i < 3; i++)
    {
      imguiIO.MouseDown[i] = inputState.mouseButtonsPressed[i] || glfwGetMouseButton(window, i) != 0;    // If a mouse press event came, always pass it as "mouse held this frame", so we don't miss click-release events that are shorter than 1 frame.
      inputState.mouseButtonsPressed[i] = false;
    }*/

    // Hide OS mouse cursor if ImGui is drawing it
    glfwSetInputMode(window, GLFW_CURSOR, imguiIO.MouseDrawCursor ? GLFW_CURSOR_HIDDEN : GLFW_CURSOR_NORMAL);
  }
  void ReloadShaders()
  {
    imGuiShader.vertex.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/ImGui/ImGui.vert.spv"));
    imGuiShader.fragment.reset(new legit::Shader(core->GetLogicalDevice(), "../data/Shaders/spirv/ImGui/ImGui.frag.spv"));
    imGuiShader.program.reset(new legit::ShaderProgram(imGuiShader.vertex.get(), imGuiShader.fragment.get()));
  }
private:
  void InitKeymap()
  {
    ImGuiIO& imguiIO = ImGui::GetIO();
    imguiIO.KeyMap[ImGuiKey_Tab] = GLFW_KEY_TAB;
    imguiIO.KeyMap[ImGuiKey_LeftArrow] = GLFW_KEY_LEFT;
    imguiIO.KeyMap[ImGuiKey_RightArrow] = GLFW_KEY_RIGHT;
    imguiIO.KeyMap[ImGuiKey_UpArrow] = GLFW_KEY_UP;
    imguiIO.KeyMap[ImGuiKey_DownArrow] = GLFW_KEY_DOWN;
    imguiIO.KeyMap[ImGuiKey_PageUp] = GLFW_KEY_PAGE_UP;
    imguiIO.KeyMap[ImGuiKey_PageDown] = GLFW_KEY_PAGE_DOWN;
    imguiIO.KeyMap[ImGuiKey_Home] = GLFW_KEY_HOME;
    imguiIO.KeyMap[ImGuiKey_End] = GLFW_KEY_END;
    imguiIO.KeyMap[ImGuiKey_Delete] = GLFW_KEY_DELETE;
    imguiIO.KeyMap[ImGuiKey_Backspace] = GLFW_KEY_BACKSPACE;
    imguiIO.KeyMap[ImGuiKey_Enter] = GLFW_KEY_ENTER;
    imguiIO.KeyMap[ImGuiKey_Escape] = GLFW_KEY_ESCAPE;
    imguiIO.KeyMap[ImGuiKey_Space] = GLFW_KEY_SPACE;
    imguiIO.KeyMap[ImGuiKey_A] = GLFW_KEY_A;
    imguiIO.KeyMap[ImGuiKey_C] = GLFW_KEY_C;
    imguiIO.KeyMap[ImGuiKey_V] = GLFW_KEY_V;
    imguiIO.KeyMap[ImGuiKey_X] = GLFW_KEY_X;
    imguiIO.KeyMap[ImGuiKey_Y] = GLFW_KEY_Y;
    imguiIO.KeyMap[ImGuiKey_Z] = GLFW_KEY_Z;
  }
  void InitCallbacks(GLFWwindow *window)
  {
    ImGuiIO& imguiIO = ImGui::GetIO();
    /*imguiIO.SetClipboardTextFn = [this]() {};
    imguiIO.GetClipboardTextFn = GetClipboardText;*/
/*#ifdef _WIN32
    imguiIO.ImeWindowHandle = glfwGetWin32Window(window);
#endif*/
    glfwSetMouseButtonCallback(window, MouseButtonCallback);
    glfwSetScrollCallback(window, ScrollCallback);
    glfwSetKeyCallback(window, KeyCallback);
    glfwSetCharCallback(window, CharCallback);
  }

  static void KeyCallback(GLFWwindow* window, int key, int, int action, int mods)
  {
    InputState *inputState = (InputState*)glfwGetWindowUserPointer(window);

    ImGuiIO& imguiIO = ImGui::GetIO();
    imguiIO.KeysDown[key] = (action != GLFW_RELEASE);

    (void)mods; // Modifiers are not reliable across systems

  }

  static void CharCallback(GLFWwindow* window, unsigned int c)
  {
    InputState *inputState = (InputState*)glfwGetWindowUserPointer(window);

    ImGuiIO& imguiIO = ImGui::GetIO();
    if (c > 0 && c < 0x10000)
      imguiIO.AddInputCharacter((unsigned short)c);
  }

  static void MouseButtonCallback(GLFWwindow *window, int button, int action, int /*mods*/)
  {
    InputState *inputState = (InputState*)glfwGetWindowUserPointer(window);

    ImGuiIO& imguiIO = ImGui::GetIO();
    if (button < 512)
    {
      imguiIO.MouseDown[button] = (action != GLFW_RELEASE);
    }
  }

  static void ScrollCallback(GLFWwindow *window, double xOffset, double yOffset)
  {
    InputState *inputState = (InputState*)glfwGetWindowUserPointer(window);

    ImGuiIO& imguiIO = ImGui::GetIO();
    imguiIO.MouseWheel += float(yOffset);
    imguiIO.MouseWheelH += float(xOffset);
  }

  /*static const char* GetClipboardText()
  {
    return glfwGetClipboardString(g_Window);
  }

  static void SetClipboardText(const char* text)
  {
    glfwSetClipboardString(g_Window, text);
  }*/

  void UploadBuffers(legit::Buffer *vertexBuffer, legit::Buffer *indexBuffer, ImDrawData* drawData)
  {
    ImDrawVert *vertBufMemory = (ImDrawVert*)vertexBuffer->Map();
    ImDrawIdx *indexBufMemory = (ImDrawIdx*)indexBuffer->Map();

    for (int cmdListIndex = 0; cmdListIndex < drawData->CmdListsCount; cmdListIndex++)
    {
      const ImDrawList* cmdList = drawData->CmdLists[cmdListIndex];

      memcpy(vertBufMemory, cmdList->VtxBuffer.Data, cmdList->VtxBuffer.Size * sizeof(ImDrawVert));
      memcpy(indexBufMemory, cmdList->IdxBuffer.Data, cmdList->IdxBuffer.Size * sizeof(ImDrawIdx));

      vertBufMemory += cmdList->VtxBuffer.Size;
      indexBufMemory += cmdList->IdxBuffer.Size;
    }
    indexBuffer->Unmap();
    vertexBuffer->Unmap();
  }

  struct InputState
  {
    float mouseWheel = 0.0f;
    double lastUpdateTime = 0.0;
    bool mouseButtonsPressed[3] = { 0 };
  }inputState;
  #pragma pack(push, 1)
  struct ImGuiVertex
  {
    glm::vec2 pos;
    glm::vec2 uv;
    glm::uint32_t color;
  };
  #pragma pack(pop)
  struct FrameResources
  {
    FrameResources(legit::Core *core, size_t maxVerticesCount, size_t maxIndicesCount)
    {
      imGuiIndexBuffer = std::unique_ptr<legit::Buffer>(new legit::Buffer(core->GetPhysicalDevice(), core->GetLogicalDevice(), sizeof(glm::uint32_t) * maxIndicesCount, vk::BufferUsageFlagBits::eIndexBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent));
      imGuiVertexBuffer = std::unique_ptr<legit::Buffer>(new legit::Buffer(core->GetPhysicalDevice(), core->GetLogicalDevice(), sizeof(ImGuiVertex) * maxVerticesCount, vk::BufferUsageFlagBits::eVertexBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent));
    }

    std::unique_ptr<legit::Buffer> imGuiIndexBuffer;
    std::unique_ptr<legit::Buffer> imGuiVertexBuffer;
  };
  std::vector<std::unique_ptr<FrameResources> > frameResources;

  void LoadImguiFont()
  {
    ImGuiIO& imGuiIO = ImGui::GetIO();

    int width, height;
    unsigned char* pixels = nullptr;
    imGuiIO.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    auto texelData = legit::CreateSimpleImageTexelData(pixels, width, height);
    auto fontCreateDesc = legit::Image::CreateInfo2d(texelData.baseSize, uint32_t(texelData.mips.size()), 1, texelData.format, vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst);

    this->fontImage = std::unique_ptr<legit::Image>(new legit::Image(core->GetPhysicalDevice(), core->GetLogicalDevice(), fontCreateDesc));
    legit::LoadTexelData(core, &texelData, fontImage->GetImageData());
    this->fontImageView = std::unique_ptr<legit::ImageView>(new legit::ImageView(core->GetLogicalDevice(), fontImage->GetImageData(), 0, fontImage->GetImageData()->GetMipsCount(), 0, 1));

    imGuiIO.Fonts->TexID = (void*)fontImageView.get();
  }

  const static uint32_t ShaderDataSetIndex = 0;
  const static uint32_t DrawCallDataSetIndex = 1;

  static legit::VertexDeclaration GetImGuiVertexDeclaration()
  {
    legit::VertexDeclaration vertexDecl;
    vertexDecl.AddVertexInputBinding(0, sizeof(ImDrawVert));
    vertexDecl.AddVertexAttribute(0, offsetof(ImDrawVert, pos), legit::VertexDeclaration::AttribTypes::vec2, 0);
    vertexDecl.AddVertexAttribute(0, offsetof(ImDrawVert, uv), legit::VertexDeclaration::AttribTypes::vec2, 1);
    vertexDecl.AddVertexAttribute(0, offsetof(ImDrawVert, col), legit::VertexDeclaration::AttribTypes::color32, 2);

    return vertexDecl;
  }

  struct ImGuiShader
  {
    #pragma pack(push, 1)
    struct ImGuiShaderData
    {
      glm::mat4 projMatrix;
    };
    #pragma pack(pop)

    std::unique_ptr<legit::Shader> vertex;
    std::unique_ptr<legit::Shader> fragment;
    std::unique_ptr<legit::ShaderProgram> program;
  } imGuiShader;

  vk::Extent2D viewportExtent;

  std::unique_ptr<legit::ImageView> fontImageView;
  std::unique_ptr<legit::Image> fontImage;

  std::unique_ptr<legit::Sampler> imageSpaceSampler;
  ImGuiContext *imguiContext;
  legit::Core *core;
};