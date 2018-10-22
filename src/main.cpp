#include "Render/Render.h"


#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>

#include <iostream>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include <chrono>
#include <ctime>
#include <memory>
#include <sstream>
int main()
{
  glfwInit();

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  GLFWwindow* window = glfwCreateWindow(800, 600, "Vulkan window", nullptr, nullptr);

  /*uint32_t extensionCount = 0;
  vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);

  std::cout << extensionCount << " extensions supported" << std::endl;*/

  uint32_t glfwExtensionCount = 0;
  const char** glfwExtensions;
  glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

  legit::WindowDesc windowDesc = {};
  windowDesc.hInstance = GetModuleHandle(NULL);
  windowDesc.hWnd = glfwGetWin32Window(window);
  auto core = std::make_unique<legit::Core>(glfwExtensions, glfwExtensionCount, &windowDesc, true);
  auto swapchain = core->CreateSwapchain(windowDesc);

  auto vertexShader   = core->CreateShaderModule("../data/Shaders/spirv/vertexShader.spv");
  auto fragmentShader = core->CreateShaderModule("../data/Shaders/spirv/fragmentShader.spv");
  //auto renderPass = core->CreateRenderPass(swapchain->GetFormat());
  //auto pipeline = core->CreatePipeline(vertexShader->GetHandle(), fragmentShader->GetHandle(), swapchain->GetSize(), renderPass->GetHandle());

  std::vector<const legit::ImageView *> swapchainImageViews = swapchain->GetImageViews();
  size_t swapchainImagesCount = swapchainImageViews.size();

  legit::RenderStateCache renderStateCache(core.get());
  
  vk::Rect2D swapchainRect = vk::Rect2D(vk::Offset2D(), swapchain->GetSize());

  std::vector<vk::UniqueCommandBuffer> commandBuffers = core->AllocateCommandBuffers(swapchainImagesCount);

  for(size_t imageIndex = 0; imageIndex < swapchainImagesCount; imageIndex++)
  {
    auto buf = commandBuffers[imageIndex].get();

    auto bufferBeginInfo = vk::CommandBufferBeginInfo()
      .setFlags(vk::CommandBufferUsageFlagBits::eSimultaneousUse);
    buf.begin(bufferBeginInfo);

    {
      renderStateCache.BeginPass(buf, { swapchain->GetImageViews()[imageIndex] }, nullptr, vertexShader.get(), fragmentShader.get(), swapchain->GetSize());
      {
        buf.draw(3, 1, 0, 0);
      }
      renderStateCache.EndPass(buf);
    }
    buf.end();
  }

  vk::UniqueSemaphore imageAvailableSemaphore = core->CreateVulkanSemaphore();
  vk::UniqueSemaphore renderFinishedSemaphore = core->CreateVulkanSemaphore();

  /*std::vector<legit::ImageView *> imageViews = {  };
  glm::mat4 matrix;
  glm::vec4 vec;
  auto test = matrix * vec;*/

  auto prevFrameTime = std::chrono::system_clock::now();
  size_t framesCount = 0;

  while (!glfwWindowShouldClose(window)) 
  {
    glfwPollEvents();
    {
      uint32_t imageIndex = swapchain->AcquireNextImage(imageAvailableSemaphore.get()).value;

      vk::Semaphore imageAvailableSemaphores[] = { imageAvailableSemaphore.get() };
      vk::Semaphore renderFinishedSemaphores[] = { renderFinishedSemaphore.get() };
      vk::PipelineStageFlags waitStages[] = { vk::PipelineStageFlagBits::eColorAttachmentOutput };

      auto submitInfo = vk::SubmitInfo()
        .setWaitSemaphoreCount(1)
        .setPWaitSemaphores(imageAvailableSemaphores)
        .setPWaitDstStageMask(waitStages)
        .setCommandBufferCount(1)
        .setPCommandBuffers(&commandBuffers[imageIndex].get())
        .setSignalSemaphoreCount(1)
        .setPSignalSemaphores(renderFinishedSemaphores);

      core->GetGraphicsQueue().submit({ submitInfo }, nullptr);

      vk::SwapchainKHR swapchains[] = { swapchain->GetHandle() };
      auto presentInfo = vk::PresentInfoKHR()
        .setSwapchainCount(1)
        .setPSwapchains(swapchains)
        .setPImageIndices(&imageIndex)
        .setPResults(nullptr)
        .setWaitSemaphoreCount(1)
        .setPWaitSemaphores(renderFinishedSemaphores);
        
      core->GetPresentQueue().presentKHR(presentInfo);

    }
    framesCount++;
    auto currFrameTime = std::chrono::system_clock::now();
    float deltaTime = std::chrono::duration<float>(currFrameTime - prevFrameTime).count();
    if (deltaTime > 0.5f)
    {
      float fps = float(framesCount) / deltaTime;
      std::stringstream title;
      title << "Fps: " << fps;
      glfwSetWindowTitle(window, title.str().c_str());
      framesCount = 0;
      prevFrameTime = currFrameTime;
    }
  }


  glfwDestroyWindow(window);

  glfwTerminate();

  return 0;
}