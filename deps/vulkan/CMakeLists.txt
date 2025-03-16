#
# Vulkan-specific dependencies
#

# Unfortunately this is currently needed earlier due to
# dear_imgui needing to link to it.. but it really should be here
#find_package(Vulkan REQUIRED COMPONENTS shaderc_combined)

set(SPIRV_CROSS_SKIP_INSTALL ON CACHE INTERNAL "")
set(SPIRV_CROSS_ENABLE_TESTS OFF CACHE INTERNAL "")
set(SPIRV_CROSS_CLI OFF CACHE INTERNAL "")
FetchContent_Declare(spirv-cross
    GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Cross.git
    GIT_TAG        "sdk-1.3.243.0")

FetchContent_Declare(vulkan_memory_allocator
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG        "v3.2.1")

FetchContent_MakeAvailable(
    spirv-cross
    vulkan_memory_allocator)

# Special hack stuff for vulkan_memory_allocator to compile it as a static lib
# (I don't want to compile this as part of any of the the main projects...)
project(VulkanMemoryAllocatorStatic)
add_library(VulkanMemoryAllocator_static VmaUsage.cpp)
target_link_libraries(VulkanMemoryAllocator_static PUBLIC GPUOpen::VulkanMemoryAllocator)
target_link_libraries(VulkanMemoryAllocator_static PRIVATE Vulkan::Vulkan)
