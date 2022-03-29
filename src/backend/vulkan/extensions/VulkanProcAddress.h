#pragma once

#define FetchProcAddr(device, function) reinterpret_cast<PFN_##function>(vkGetDeviceProcAddr(device, #function))
