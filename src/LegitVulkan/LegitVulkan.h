#pragma once
#define VK_USE_PLATFORM_WIN32_KHR
//#include <vulkan/vulkan.h>
//#include <vulkan/vk_sdk_platform.h>
#define NOMINMAX
#include <vulkan/vulkan.hpp>

//#include <Windows.h>

#include <glm/glm.hpp>
#include "../ImGuiUtils/ProfilerTask.h" 
#include "Handles.h"
#include "Pool.h"
#include "CpuProfiler.h"
#include "QueueIndices.h"
#include "Surface.h"
#include "VertexDeclaration.h"
#include "ShaderModule.h"
#include "ShaderProgram.h"
#include "Buffer.h"
#include "Image.h"
#include "Synchronization.h"
#include "TimestampQuery.h"
#include "GpuProfiler.h"
#include "Sampler.h"
#include "Pipeline.h"
#include "ImageView.h"
#include "Swapchain.h"
#include "RenderPass.h"
#include "Framebuffer.h"
#include "ShaderMemoryPool.h"
#include "DescriptorSetCache.h"
#include "PipelineCache.h"
#include "RenderPassCache.h"

#include "Core.h"
#include "RenderGraph.h"
#include "CoreImpl.h"

#include "PresentQueue.h"

#include "StagedResources.h"
#include "ImageLoader.h"
