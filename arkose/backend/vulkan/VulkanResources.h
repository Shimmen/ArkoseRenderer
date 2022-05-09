#pragma once

// These are only used in the common vulkan backend files, e.g. VulkanBackend & VulkanCommandList so there is no perf reason to split this up.
#include "backend/vulkan/VulkanBuffer.h"
#include "backend/vulkan/VulkanTexture.h"
#include "backend/vulkan/VulkanRenderTarget.h"
#include "backend/vulkan/VulkanBindingSet.h"
#include "backend/vulkan/VulkanRenderState.h"
#include "backend/vulkan/VulkanComputeState.h"

// Nvidia's NV ray tracing extension
#include "backend/vulkan/extensions/ray-tracing-nv/VulkanAccelerationStructureNV.h"
#include "backend/vulkan/extensions/ray-tracing-nv/VulkanRayTracingStateNV.h"

// Khronos KHR ray tracing extension
#include "backend/vulkan/extensions/ray-tracing-khr/VulkanAccelerationStructureKHR.h"
#include "backend/vulkan/extensions/ray-tracing-khr/VulkanRayTracingStateKHR.h"
