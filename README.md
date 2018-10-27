# LegitEngine
A simple Vulkan framework with approachable architecture

# Why?
While there are plenty of Vulkan tutorials around, most of them follow the most simple monolith/monobrick program architecture similar to (example from https://vulkan-tutorial.com/)
```cpp
    void app::initVulkan() {
        createInstance();
        setupDebugCallback();
        createSurface();
        pickPhysicalDevice();
        createLogicalDevice();
        createSwapChain();
        createImageViews();
        createRenderPass();
        createGraphicsPipeline();
        createFramebuffers();
        createCommandPool();
        createVertexBuffer();
        createCommandBuffers();
        createSyncObjects();
    }
    
    void app::cleanup() {
        cleanupSwapChain();

        vkDestroyBuffer(device, vertexBuffer, nullptr);
        vkFreeMemory(device, vertexBufferMemory, nullptr);
        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            vkDestroySemaphore(device, renderFinishedSemaphores[i], nullptr);
            vkDestroySemaphore(device, imageAvailableSemaphores[i], nullptr);
            vkDestroyFence(device, inFlightFences[i], nullptr);
        }
        //a lot more code here
    }
```

In my opinion, not only this approach is very prone to errors due to mismatching 
resource allocation/deallocation which have to be done manually, it also fails to create a clear picture of how components interact with each other. 
In my opinion in order to be able to use an API on any meaningful level, it's essential to understand the way its components are supposed to be grouped and connected with each other.

# What is this?

This is a basic vulkan framework that's made for learning purposes, but follows different goals than most other Vulkan tutorial frameworks. 
Its "meat" is mostly based on aforementioned https://vulkan-tutorial.com/ that explains very well logic behind Vulkan GAPI calls, 
however, I had different goals in mind when creating it:
1) Using <vulkan/vulkan.hpp> provides very important benefits compared to plain C api:
   - compiletime type safety for all flags
   - automatic resource management via built-in unique pointers eliminates all resource deallocation code and eliminates a lot of potential errors
   - better syntax with less repetion
   - use of modern language features and C++ containers such as std::vector<>
   - automatic and seamless loader for extensions
   - full compatibility with plain C api if that's needed
2) My main goal is not to try and make use of all Vulkan's overwhelming flexibility and power, but rather to create a convenient framework with familiar interface similar to what
most other rendering engines have. 
3) Even though there is indeed some price for doing things the old-fashioned way, it has a clear benefit of greatly simplifying framework's interface
5) Only familiar things stick outside from the framework completely isolating user code from automatically managed 
complex features such as pipelines, render passes and synchonization primitives.
4) I entirely avoided eny explicit resource deallocation and memory management by using unique pointers and exceptions that in my opinion not only makes code safer but also easier to understand
 
Here's what an example of a simple program that draws a triangle with this framework can look like:
```cpp
int main()
{
  glfwInit();

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  GLFWwindow* window = glfwCreateWindow(800, 600, "Vulkan window", nullptr, nullptr);

  uint32_t glfwExtensionCount = 0;
  const char** glfwExtensions;
  glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

  legit::WindowDesc windowDesc = {};
  windowDesc.hInstance = GetModuleHandle(NULL);
  windowDesc.hWnd = glfwGetWin32Window(window);
  //single core instance that can be created headless if windowDesc is nullptr
  //which can be useful for compute-operations only without rendering on a window
  auto core = std::make_unique<legit::Core>(glfwExtensions, glfwExtensionCount, &windowDesc, true);

  auto vertexShader = core->CreateShaderModule("../data/Shaders/spirv/vertexShader.spv");
  auto fragmentShader = core->CreateShaderModule("../data/Shaders/spirv/fragmentShader.spv");

  legit::RenderStateCache renderStateCache(core.get());
  std::unique_ptr<legit::PresentQueue> presentQueue;
 
#pragma pack(push, 1)
  struct Vertex
  {
    glm::vec3 pos;
    glm::vec4 color;
    glm::vec2 uv;
  };
#pragma pack(pop)

  std::vector<Vertex> vertices;
  legit::VertexDeclaration vertexDecl;
  {
    vertexDecl.AddVertexInputBinding(0, sizeof(Vertex));
    vertexDecl.AddVertexAttribute(0, offsetof(Vertex, pos),   legit::VertexDeclaration::AttribTypes::vec3, 0);
    vertexDecl.AddVertexAttribute(0, offsetof(Vertex, color), legit::VertexDeclaration::AttribTypes::vec4, 1);
    vertexDecl.AddVertexAttribute(0, offsetof(Vertex, uv),    legit::VertexDeclaration::AttribTypes::vec2, 2);
  }
  vertices.push_back(Vertex{ glm::vec3(0.5f, 0.0f, 0.0f), glm::vec4(1.0f, 0.0f, 0.0f, 1.0f), glm::vec2(0.0f, 0.0f) });
  vertices.push_back(Vertex{ glm::vec3(0.5f, 1.0f, 0.0f), glm::vec4(0.0f, 1.0f, 0.0f, 1.0f), glm::vec2(0.0f, 0.0f) });
  vertices.push_back(Vertex{ glm::vec3(1.0f, 0.5f, 0.0f), glm::vec4(0.0f, 0.0f, 1.0f, 1.0f), glm::vec2(0.0f, 0.0f) });

  auto vertexBuffer = std::make_unique<legit::StagedBuffer>(core.get(), vertices.size() * sizeof(Vertex), vk::BufferUsageFlagBits::eVertexBuffer);
  while (!glfwWindowShouldClose(window))
  {
    glfwPollEvents();
    if(!presentQueue)
    {
      //when device is lost we recreate only part of the engine that depends on swapchain
      //it's possible to manage multiple windows with a single instance of core
      presentQueue = std::unique_ptr<legit::PresentQueue>(new legit::PresentQueue(core.get(), windowDesc));
    }

    for (auto &vertex : vertices)
    {
      //vertices can be changed dynamically here
    }

    try
    {
      auto frameInfo = presentQueue->BeginFrame();
      {
        void *bufferMem = vertexBuffer->Map();
        memcpy(bufferMem, vertices.data(), vertices.size() * sizeof(Vertex));
        //memory synchonization is achieved automatically within implementation of vertexBuffer
        vertexBuffer->Unmap(frameInfo.commandBuffer);

        //pipeline and render pass management is done automatically
        renderStateCache.BeginPass(frameInfo.commandBuffer, { frameInfo.imageView }, nullptr, vertexDecl, vertexShader.get(), fragmentShader.get(), presentQueue->GetSize() );
        {
          frameInfo.commandBuffer.bindVertexBuffers(0, { vertexBuffer->GetBuffer() }, { 0 });
          frameInfo.commandBuffer.draw(vertices.size(), 1, 0, 0);
        }
        //presentation and graphics pipeline synchronization is done automatically
        renderStateCache.EndPass(frameInfo.commandBuffer);
      }
      presentQueue->EndFrame();
    }
    catch (vk::OutOfDateKHRError err)
    {
      core->WaitIdle();
      presentQueue.reset();
    }
  }
  return 0;
}
```

# What is this not?
This is by no means a complete engine ready for production. A lot of basic functionality such as handling textures and shader uniforms is lacking. It's made for learning purposes and hopefully can help someone build a clearer picture of how Vulkan renderer can be organized.
