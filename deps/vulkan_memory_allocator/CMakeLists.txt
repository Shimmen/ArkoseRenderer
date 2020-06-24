cmake_minimum_required(VERSION 3.0)

project(vulkan_memory_allocator LANGUAGES CXX)
add_library(vulkan_memory_allocator vk_mem_alloc.cpp)
target_include_directories(vulkan_memory_allocator PUBLIC .)
target_link_libraries(vulkan_memory_allocator PRIVATE Vulkan::Vulkan)
