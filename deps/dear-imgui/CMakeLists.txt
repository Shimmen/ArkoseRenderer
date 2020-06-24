cmake_minimum_required(VERSION 3.0)

project(dear_imgui LANGUAGES CXX)
add_library(dear_imgui imgui.cpp imgui_draw.cpp imgui_widgets.cpp imgui_demo.cpp imgui_impl_glfw.cpp imgui_impl_vulkan.cpp)
target_include_directories(dear_imgui PUBLIC .)

target_link_libraries(dear_imgui glfw)

target_include_directories(dear_imgui PRIVATE Vulkan::Vulkan)
target_link_libraries(dear_imgui Vulkan::Vulkan)
