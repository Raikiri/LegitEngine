# LegitEngine
This is thin rendergraph-based layer between vulkan and you that does most of the synchronization and resource management so that you don't have to. Quite legit too.

# Why?
If you have tried learning vulkan from the popular online tutorials, you're probably used to thinking that vulkan is extremely verbose and cumbersome API that leads to unreadable blocks of monocode such as example from https://vulkan-tutorial.com/ :
<details><summary>Monobrick example</summary>
<p>
    
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

</p>
</details>

Those tutorials are great at telling you what different API calls do, but in my opinion the hardest part is actually figuring out how to make a scaleable renderer from those calls, because there's always so many ways to do the same thing and it is often really unclear how it's all actually supposed to come together into something practically useful. For analogy, it's like learning asm: yes, it does give you a deep understanding of how low-level programming works, but it gives you zero insight on how to create a useful and scalable engine from it.

# What is this?

This is a graphics framework based on a concept of a render graph: modern way of designing a realtime renderer where you first define what's going to happen in your entire frame and then the engine can figure out on how to execute it optimally, safely and doing as much synchronization heavylifting automatically as possible. For example, if you say that you're going to use a texture as a rendertarget first and then you're going to sample from it, rendergraph can create all needed barriers just from this information.

# What are its design goals?

My first goal is to provide a way of grouping vulkan entities into objects that are meaningful to directly interact with. For example, a shader class that gives you full reflection data and convenient interface to bind resources. Or an isolated in-flight-queue that does all the image presentation onto a window. 

My second goal is to automate everything that's possible to automate effectively because of access to the structure of the whole frame via render graph. For example, instead of putting image or buffer barriers manually, you're telling graph how you're going to use your images and buffers, and it puts all the barriers automatically. You don't need to deal with many intermediate vulkan entities at all, such as render passes, pipeline layouts, descriptor layouts, etc. 

My third and final goal is to show some good (from my experience) design practices for typical vulkan usage problems that have multiple possible solutions. For example, how do you upload shader resources to materials multiple objects? What if they have the same material? How do make a renderer that can be used for rendering onto a window and headless? How do you manage descriptor sets? How do you implement it using modern type-safe C++?

# What can it do?
I do a lot of experimentation with various graphics-related and GPGPU algorithms, and this framework has now become my go-to choice for prototyping anything from GI techniques to FFT and physics algorithms. It provides low enough access to GPU to be suitable for the vast majority of my needs and yet convenient enough to be effective when just used directly with vulkan. I spend a little bit more time prototyping stuff with LegitEngine than I would by using an OpenGL-based framework, but then I save enormous amount of time on debugging and profiling because it's designed to be super-easy to do it.

# Minimal code example
Here's a minimal code example that creates a window, initializes the renderer, creates an in-flight queue with a swapchain attached to that window, handles window resizing/swapchain recreation, and renders a simple fullscreen shader.
```cpp
GLFWwindow* window = glfwCreateWindow(1024, 1024, "Legit Vulkan renderer", nullptr, nullptr);
  
const char* glfwExtensions[] = { "VK_KHR_surface", "VK_KHR_win32_surface" };
uint32_t glfwExtensionCount = 2;

legit::WindowDesc windowDesc = {};
windowDesc.hInstance = GetModuleHandle(NULL);
windowDesc.hWnd = glfwGetWin32Window(window);

auto core = std::make_unique<legit::Core>(glfwExtensions, glfwExtensionCount, &windowDesc, true);

while (glfwWindowShouldClose(window))
{
  glfwPollEvents();
  if (!inFlightQueue)
  {
    core->ClearCaches();
    inFlightQueue = std::unique_ptr<legit::InFlightQueue>(new legit::InFlightQueue(core.get(), windowDesc, 2, vk::PresentModeKHR::eMailbox));
  }
  
  try
  {
    auto frameInfo = inFlightQueue->BeginFrame();
    {
      core->GetRenderGraph()->AddPass(legit::RenderGraph::RenderPassDesc()
        .SetColorAttachments({ { frameInfo.swapchainImageViewProxyId, vk::AttachmentLoadOp::eDontCare } })
        .SetRenderAreaExtent(this->viewportExtent)
        .SetRecordFunc([this](legit::RenderGraph::RenderPassContext passContext)
      {
        auto pipeineInfo = core->GetPipelineCache()->BindGraphicsPipeline(passContext.GetCommandBuffer(), passContext.GetRenderPass()->GetHandle(), legit::DepthSettings::Disabled(), { legit::BlendSettings::Opaque() }, legit::VertexDeclaration(), vk::PrimitiveTopology::eTriangleFan, shader.program.get());
        passContext.GetCommandBuffer().draw(4, 1, 0, 0);
      }));
    }
    inFlightQueue->EndFrame();
  }
  catch (vk::OutOfDateKHRError err)
  {
    core->WaitIdle();
    inFlightQueue.reset();
  }
}
core->WaitIdle();

glfwDestroyWindow(window);
```
# Demos
I have included a bunch of very different demos to show how very different algorithms can be implemented using LegitEngine. Here's a quick description of what every demo does.

### Screenspace Variance Global Illumination
![image](https://user-images.githubusercontent.com/1657728/76221721-5f26c880-627e-11ea-8f2d-def8a1e0c90d.png)
This is a GI algorithm that calculates unbiased second-bounce diffuse global illumination for geometry in gBuffer. It's fast enough to be used in realtime and I have implemented a version of it for Path of Exile. Here's a showcase of what it can do: https://www.youtube.com/watch?v=OPFvcsQAKjc Version in this repo does not have a denoiser though.

### Volume renderer
![image](https://user-images.githubusercontent.com/1657728/76222133-06a3fb00-627f-11ea-8880-80403cc7d901.png)
I have included a very simple unbiased volumetric path tracer. Its code looks more complex than it is, because as like my other demos, it has a lot of experimental code left, most of it is not really used. There are some more pre-rendered images in bin/data/Screenshots/VolumeRenderer folder.

### Textured point sprite renderer
![image](https://user-images.githubusercontent.com/1657728/76222384-67cbce80-627f-11ea-924d-fc6f8073dc28.png)
This is an algorithm for rendering big numbers of transparent particles in sorted fashion where particles are first put into buckets of their own size, then sorted and then rendered on screen back-to-front. This algorithm relies heavily on interaction of compute shaders, render passes, synchronization and all kinds of GPU resources. Added benefit is that this representation is very convenient for doing coherent raycasting so I also implemented a GI on it. A demo of this algorithm is showcased here: https://www.youtube.com/watch?v=e8Jsq1K-nPk

### Shrodinger fluid dynamics solver
![image](https://user-images.githubusercontent.com/1657728/76224694-a6ae5400-6280-11ea-978a-bb5eadaf1ad6.png)
This is my implementation of this paper http://page.math.tu-berlin.de/~chern/projects/PhDThesis/thesis_reduced.pdf that proposes a way of solving a navier-stokes system of hydrodynamics equations by transforming them into shrodinger's equations that are easier to solve. Shrodinger equations are solved analytically in spectral domain, transformation is done via 3d FFT that's implemented for GPU as well via a compute shader. Point sprite renderer is used to visualize flow direction. Here's a link to more gifs: https://imgur.com/gallery/fIkx6rC

# Licensing
You're free to use any parts of this code in your private as well as commercial projects as long you leave an attribution somewhere. MIT license, basically. I'd also appreciate if you let me know that you found it useful. The rights to all of the models, volumes and textures in this repo, however, belong to their respective owners (not me).

# What can it not do?
This is not a multirender abstraction over GAPI. It's too low level for that and is designed specifically to make interaction with vulkan more pleasant. It's also not multithreaded. Async resource streaming needs to be implemented outside of the framework. This framework is far from being optimized : it uses many unnecessary allocations here and there, all caches are done on std::map's instead of hash maps, simply because my main priority is its macro-architecture, not micro-optimizations that can always come later if/when needed.

