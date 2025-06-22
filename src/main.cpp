#include "LegitVulkan/LegitVulkan.h"


#define GLM_FORCE_RADIANS
#define GLM_DEPTH_ZERO_TO_ONE
//#define GLM_FORCE_RIGHT_HANDED
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>
#include <glm/gtx/transform.hpp>
#include <tiny_obj_loader.h>

#include <iostream>

#include "LegitGLFW/LegitGLFW.h"

#include "json/json.h"
#include <chrono>
#include <ctime>
#include <memory>
#include <sstream>
#include <array>

glm::vec3 ReadJsonVec3f(Json::Value vectorValue)
{
  return glm::vec3(vectorValue[0].asFloat(), vectorValue[1].asFloat(), vectorValue[2].asFloat());
}
glm::ivec2 ReadJsonVec2i(Json::Value vectorValue)
{
  return glm::ivec2(vectorValue[0].asInt(), vectorValue[1].asInt());
}

glm::uvec3 ReadJsonVec3u(Json::Value vectorValue)
{
  return glm::uvec3(vectorValue[0].asUInt(), vectorValue[1].asUInt(), vectorValue[2].asUInt());
}

#include "Scene/Mesh.h"
#include "Scene/Scene.h"
#include "imgui.h"
#include "LegitProfiler/ImGuiProfilerRenderer.h"
#include "LegitImGui/ImGuiRenderer.h"
#include "Render/Renderers/BaseRenderer.h"
#include "Render/Renderers/SSVGIRenderer.h"
#include "Render/Renderers/VolumeRenderer.h"
#include "Render/Renderers/PointRenderer.h"
#include "Render/Renderers/WaterRenderer/WaterParticleRenderer.h"


struct ImGuiScopedFrame
{
  ImGuiScopedFrame()
  {
    ImGui::NewFrame();
  }
  ~ImGuiScopedFrame()
  {
    ImGui::EndFrame();
  }
};


std::unique_ptr<BaseRenderer> CreateRenderer(legit::Core* core, std::string name)
{
  if (name == "SSVGIRenderer")
    return std::unique_ptr<BaseRenderer>(new SSVGIRenderer(core));
  if (name == "VolumeRenderer")
    return std::unique_ptr<BaseRenderer>(new VolumeRenderer(core));
  if (name == "PointRenderer")
    return std::unique_ptr<BaseRenderer>(new PointRenderer(core));
  if (name == "WaterRenderer")
    return std::unique_ptr<BaseRenderer>(new WaterParticleRenderer(core));
  assert(!!"Wrong renderer specified");
  return nullptr;
}

glm::uvec2 GetGlfwWindowClientSize(GLFWwindow *window)
{
  int width = 0, height = 0;
  glfwGetWindowSize(window, &width, &height);
  return {width, height};
}

int RunDemo(int currDemo, legit::WindowFactory::Window *window)
{
  std::string configFilename;
  Scene::GeometryTypes geomType;
  std::string rendererName;

  if(currDemo == 0)
  {
    configFilename = "../data/Scenes/DummyScene.json";
    geomType = Scene::GeometryTypes::Triangles;
    rendererName = "WaterRenderer";
  }

  if (currDemo == 1)
  {
    configFilename = "../data/Scenes/SponzaSceneLight.json";
    geomType = Scene::GeometryTypes::SizedPoints;
    rendererName = "PointRenderer";
  }

  if (currDemo == 2)
  {
    configFilename = "../data/Scenes/SponzaScene.json";
    geomType = Scene::GeometryTypes::Triangles;
    rendererName = "SSVGIRenderer";
  }
  if(currDemo == 3)
  {
    configFilename = "../data/Scenes/DummyScene.json";
    geomType = Scene::GeometryTypes::Triangles;
    rendererName = "VolumeRenderer";
  }

  int nextDemo = currDemo;
  bool isClosed = false;

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

  {
    bool enableDebugging = false;
    #if defined LEGIT_ENABLE_DEBUGGING
    enableDebugging = true;
    #endif
    
    legit::WindowDesc windowDesc = window->GetWindowDesc();

    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    auto core = std::make_unique<legit::Core>(glfwExtensions, glfwExtensionCount, &windowDesc, enableDebugging);

    ImGuiRenderer imguiRenderer(core.get(), window->glfw_window);
    ImGuiUtils::ProfilersWindow profilersWindow;

    Json::Value configRoot;
    Json::Reader reader;




    std::ifstream fileStream(configFilename);
    if (!fileStream.is_open())
      std::cout << "Can't open scene file";
    bool result = reader.parse(fileStream, configRoot);
    if (result)
    {
      std::cout << "File " << configFilename << ", parsing successful\n";
    }
    else
    {
      std::cout << "Error: File " << configFilename << ", parsing failed with errors: " << reader.getFormattedErrorMessages() << "\n";
    }
    Scene scene(configRoot["scene"], core.get(), geomType);

    std::unique_ptr<BaseRenderer> renderer;


    renderer = CreateRenderer(core.get(), rendererName);

    renderer->RecreateSceneResources(&scene);

    std::unique_ptr<legit::InFlightQueue> inFlightQueue;

    Camera light;
    //light.pos = glm::vec3(0.5f, 2.7f, -0.5f);
    light.pos = glm::vec3(0.0f, 5.0f, 0.0f);
    light.vertAngle = 3.1415f / 2.0f;

    Camera camera;
    camera.pos = glm::vec3(0.0f, 0.5f, -2.0f);

    auto startTime = std::chrono::system_clock::now();

    auto prevFrameTime = startTime;
    size_t framesCount = 0;

    glm::f64vec2 mousePos;
    glfwGetCursorPos(window->glfw_window, &mousePos.x, &mousePos.y);

    glm::f64vec2 prevMousePos = mousePos;

    size_t frameIndex = 0;
    while (!(isClosed = glfwWindowShouldClose(window->glfw_window)) && currDemo == nextDemo)
    {
      auto currFrameTime = std::chrono::system_clock::now();
      float deltaTime = std::chrono::duration<float>(currFrameTime - prevFrameTime).count();
      prevFrameTime = currFrameTime;

      glfwPollEvents();
      {
        imguiRenderer.ProcessInput(window->glfw_window);

        glfwGetCursorPos(window->glfw_window, &mousePos.x, &mousePos.y);

        if (!inFlightQueue)
        {
          std::cout << "recreated\n";
          core->ClearCaches();
          //core->GetRenderGraph()->Clear();
          inFlightQueue = std::unique_ptr<legit::InFlightQueue>(new legit::InFlightQueue(core.get(), windowDesc, GetGlfwWindowClientSize(window->glfw_window), 2, vk::PresentModeKHR::eMailbox));
          renderer->RecreateSwapchainResources(inFlightQueue->GetImageSize(), inFlightQueue->GetInFlightFramesCount());
          imguiRenderer.RecreateSwapchainResources(inFlightQueue->GetImageSize(), inFlightQueue->GetInFlightFramesCount());
        }

        auto& imguiIO = ImGui::GetIO();
        imguiIO.DeltaTime = 1.0f / 60.0f;              // set the time elapsed since the previous frame (in seconds)
        imguiIO.DisplaySize.x = float(inFlightQueue->GetImageSize().width);             // set the current display width
        imguiIO.DisplaySize.y = float(inFlightQueue->GetImageSize().height);             // set the current display height here

        if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow))
        {
          glm::vec3 dir = glm::vec3(0.0f, 0.0f, 0.0f);

          if (glfwGetMouseButton(window->glfw_window, GLFW_MOUSE_BUTTON_2))
          {
            float mouseSpeed = 0.01f;
            if (mousePos != prevMousePos)
              renderer->ChangeView();
            camera.horAngle += float((mousePos - prevMousePos).x * mouseSpeed);
            camera.vertAngle += float((mousePos - prevMousePos).y * mouseSpeed);
          }
          glm::mat4 cameraTransform = camera.GetTransformMatrix();
          glm::vec3 cameraForward = glm::vec3(cameraTransform * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f));
          glm::vec3 cameraRight = glm::vec3(cameraTransform * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
          glm::vec3 cameraUp = glm::vec3(cameraTransform * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f));


          if (glfwGetKey(window->glfw_window, GLFW_KEY_E))
            dir += glm::vec3(0.0f, -0.0f, 1.0f);
          if (glfwGetKey(window->glfw_window, GLFW_KEY_S))
            dir += glm::vec3(-1.0f, 0.0f, 0.0f);
          if (glfwGetKey(window->glfw_window, GLFW_KEY_D))
            dir += glm::vec3(0.0f, 0.0f, -1.0f);
          if (glfwGetKey(window->glfw_window, GLFW_KEY_F))
            dir += glm::vec3(1.0f, 0.0f, 0.0f);
          if (glfwGetKey(window->glfw_window, GLFW_KEY_SPACE))
            dir += glm::vec3(0.0f, 1.0f, 0.0f);
          if (glfwGetKey(window->glfw_window, GLFW_KEY_C))
            dir += glm::vec3(0.0f, -1.0f, 0.0f);

          if (glm::length(dir) > 0.0f)
            renderer->ChangeView();

          float cameraSpeed = 3.0f;
          camera.pos += cameraForward * dir.z * cameraSpeed * deltaTime;
          camera.pos += cameraRight * dir.x * cameraSpeed * deltaTime;
          camera.pos += cameraUp * dir.y * cameraSpeed * deltaTime;

          if (glfwGetKey(window->glfw_window, GLFW_KEY_V))
          {
            renderer->ReloadShaders();
          }
        }

        if (glfwGetKey(window->glfw_window, GLFW_KEY_1))
        {
          nextDemo = 0;
          glfwSetWindowShouldClose(window->glfw_window, GLFW_TRUE);
        }

        const uint32_t FrameSetIndex = 0;
        const uint32_t PassSetIndex = 1;
        const uint32_t DrawCallSetIndex = 2;

        try
        {
          auto frameInfo = inFlightQueue->BeginFrame();
          {
            ImGuiScopedFrame scopedFrame;

            auto& gpuProfilerData = inFlightQueue->GetLastFrameGpuProfilerData();
            auto& cpuProfilerData = inFlightQueue->GetLastFrameCpuProfilerData();

            {
              auto passCreationTask = inFlightQueue->GetCpuProfiler().StartScopedTask("PassCreation", legit::Colors::orange);
              renderer->RenderFrame(frameInfo, camera, light, &scene, window->glfw_window);
            }
            if (!profilersWindow.stopProfiling)
            {
              auto profilersTask = inFlightQueue->GetCpuProfiler().StartScopedTask("Prf processing", legit::Colors::sunFlower);

              profilersWindow.gpuGraph.LoadFrameData(gpuProfilerData.data(), gpuProfilerData.size());
              profilersWindow.cpuGraph.LoadFrameData(cpuProfilerData.data(), cpuProfilerData.size());
            }

            {
              auto profilersTask = inFlightQueue->GetCpuProfiler().StartScopedTask("Prf rendering", legit::Colors::belizeHole);
              profilersWindow.Render();
            }

            ImGui::Begin("Demo controls", 0, ImGuiWindowFlags_NoScrollbar);
            {
              ImGui::Text("esdf, c, space: move camera");
              ImGui::Text("v: live reload shaders");
              //ImGui::LabelText("right mouse button: rotate camera");
              ImGui::RadioButton("WaterRenderer", &nextDemo, 0);
              ImGui::RadioButton("PointRenderer", &nextDemo, 1);
              ImGui::RadioButton("SSVGIRenderer", &nextDemo, 2);
              ImGui::RadioButton("VolumeRenderer", &nextDemo, 3);
            }
            ImGui::End();

            //ImGui::ShowStyleEditor();
            //ImGui::ShowDemoWindow();


            ImGui::Render();
            imguiRenderer.RenderFrame(frameInfo, window->glfw_window, ImGui::GetDrawData());
          }
          inFlightQueue->EndFrame();
        }
        catch (vk::OutOfDateKHRError err)
        {
          core->WaitIdle();
          inFlightQueue.reset();
        }

        prevMousePos = mousePos;
      }
    }

    core->WaitIdle();
  }
  return isClosed ? -1 : nextDemo;
}
int main(int argsCount, char **args)
{
  int currDemo = 0;
  auto windowFactory = legit::WindowFactory();
  auto window = windowFactory.Create(1024, 1024, "Legit engine!", nullptr, nullptr);
  while (currDemo != -1)
  {
    currDemo = RunDemo(currDemo, window.get());
  }
  

  return 0;
}