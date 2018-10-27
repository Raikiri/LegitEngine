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

  auto vertexShader = core->CreateShaderModule("../data/Shaders/spirv/vertexShader.spv");
  auto fragmentShader = core->CreateShaderModule("../data/Shaders/spirv/fragmentShader.spv");

  legit::RenderStateCache renderStateCache(core.get());
  std::unique_ptr<legit::PresentQueue> presentQueue;
 
  /*std::vector<legit::ImageView *> imageViews = {  };
  glm::mat4 matrix;
  glm::vec4 vec;
  auto test = matrix * vec;*/
  auto prevFrameTime = std::chrono::system_clock::now();
  size_t framesCount = 0;

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
    {
      if(!presentQueue)
      {
        std::cout << "recreated\n";
        presentQueue = std::unique_ptr<legit::PresentQueue>(new legit::PresentQueue(core.get(), windowDesc));
      }

      glm::vec2 dir = glm::vec2(0.0f, 0.0f);

      if (glfwGetKey(window, GLFW_KEY_E))
        dir += glm::vec2(0.0f, -1.0f);
      if (glfwGetKey(window, GLFW_KEY_S))
        dir += glm::vec2(-1.0f, 0.0f);
      if (glfwGetKey(window, GLFW_KEY_D))
        dir += glm::vec2(0.0f, 1.0f);
      if (glfwGetKey(window, GLFW_KEY_F))
        dir += glm::vec2(1.0f, 0.0f);

      for (auto &vertex : vertices)
      {
        glm::vec3 offset = glm::vec3(dir * 0.1f, 0.0f);
        vertex.pos += offset;
      }

      try
      {
        auto frameInfo = presentQueue->BeginFrame();
        {
          void *bufferMem = vertexBuffer->Map();
          memcpy(bufferMem, vertices.data(), vertices.size() * sizeof(Vertex));
          vertexBuffer->Unmap(frameInfo.commandBuffer);

          renderStateCache.BeginPass(frameInfo.commandBuffer, { frameInfo.imageView }, nullptr, vertexDecl, vertexShader.get(), fragmentShader.get(), presentQueue->GetSize() );
          {
            frameInfo.commandBuffer.bindVertexBuffers(0, { vertexBuffer->GetBuffer() }, { 0 });
            frameInfo.commandBuffer.draw(3, 1, 0, 0);
          }
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

  core->WaitIdle();


  glfwDestroyWindow(window);

  glfwTerminate();

  return 0;
}