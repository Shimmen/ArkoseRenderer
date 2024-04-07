#pragma once

#define FetchVulkanInstanceProcAddr(instance, function) reinterpret_cast<PFN_##function>(vkGetInstanceProcAddr(instance, #function))
#define FetchVulkanDeviceProcAddr(device, function) reinterpret_cast<PFN_##function>(vkGetDeviceProcAddr(device, #function))
