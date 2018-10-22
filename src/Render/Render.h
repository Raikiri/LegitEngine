#define VK_USE_PLATFORM_WIN32_KHR
//#include <vulkan/vulkan.h>
//#include <vulkan/vk_sdk_platform.h>
#include <vulkan/vulkan.hpp>

#define NOMINMAX
#include <Windows.h>

#include <glm/glm.hpp>

#include "QueueIndices.h"
#include "Surface.h"
#include "ShaderModule.h"
#include "Pipeline.h"
#include "ImageView.h"
#include "Swapchain.h"
#include "RenderPass.h"
#include "Framebuffer.h"
#include "Core.h"
#include "RenderStateCache.h"
