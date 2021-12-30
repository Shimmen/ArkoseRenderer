#pragma once

// These are only used in the common vulkan backend files, e.g. VulkanBackend & VulkanCommandList so there is no perf reason to split this up.
#include "backend/vulkan/VulkanBuffer.h"
#include "backend/vulkan/VulkanTexture.h"
#include "backend/vulkan/VulkanRenderTarget.h"
#include "backend/vulkan/VulkanBindingSet.h"
#include "backend/vulkan/VulkanRenderState.h"
#include "backend/vulkan/VulkanAccelerationStructure.h"
#include "backend/vulkan/VulkanRayTracingState.h"
#include "backend/vulkan/VulkanComputeState.h"
