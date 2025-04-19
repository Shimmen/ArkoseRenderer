#pragma once

// These are only used in the common vulkan backend files, e.g. VulkanBackend & VulkanCommandList so there is no perf reason to split this up.
#include "rendering/backend/vulkan/VulkanBuffer.h"
#include "rendering/backend/vulkan/VulkanTexture.h"
#include "rendering/backend/vulkan/VulkanRenderTarget.h"
#include "rendering/backend/vulkan/VulkanBindingSet.h"
#include "rendering/backend/vulkan/VulkanRenderState.h"
#include "rendering/backend/vulkan/VulkanComputeState.h"

// Khronos KHR ray tracing extension
#include "rendering/backend/vulkan/extensions/ray-tracing-khr/VulkanAccelerationStructureKHR.h"
#include "rendering/backend/vulkan/extensions/ray-tracing-khr/VulkanRayTracingStateKHR.h"
