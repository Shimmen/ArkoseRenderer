cmake_minimum_required(VERSION 3.0)

project(dear_imgui LANGUAGES CXX)
add_library(dear_imgui imgui.cpp imgui_draw.cpp imgui_widgets.cpp imgui_tables.cpp imgui_demo.cpp backends/imgui_impl_glfw.cpp backends/imgui_impl_vulkan.cpp ImGuizmo.cpp)
target_compile_features(dear_imgui PRIVATE cxx_std_11)
target_include_directories(dear_imgui PUBLIC .)

target_link_libraries(dear_imgui glfw)

target_include_directories(dear_imgui PRIVATE Vulkan::Vulkan)
target_link_libraries(dear_imgui Vulkan::Vulkan)
