cmake_minimum_required(VERSION 3.8 FATAL_ERROR)
project(ArkoseRenderer)

include(FetchContent)

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})

add_executable(ArkoseRenderer
    src/apps/TestApp.cpp
    src/apps/SimpleApp.cpp
    src/apps/MultisampleTest.cpp
    src/main.cpp
    src/backend/Backend.cpp
    src/backend/Resources.cpp
    src/backend/vulkan/VulkanBackend.cpp
    src/backend/vulkan/VulkanCommandList.cpp
    src/backend/vulkan/VulkanResources.cpp
    src/backend/vulkan/VulkanRTX.cpp
    src/rendering/Shader.cpp
    src/rendering/ShaderManager.cpp
    src/rendering/Registry.cpp
    src/rendering/RenderGraphNode.cpp
    src/rendering/RenderGraph.cpp
    src/rendering/camera/FpsCamera.cpp
    src/rendering/scene/Scene.cpp
    src/rendering/scene/Material.cpp
    src/rendering/scene/Mesh.cpp
    src/rendering/scene/Model.cpp
    src/rendering/scene/Vertex.cpp
    src/rendering/scene/models/GltfModel.cpp
    src/rendering/scene/Light.cpp
    src/rendering/nodes/BloomNode.cpp
    src/rendering/nodes/DebugForwardNode.cpp
    src/rendering/nodes/ExposureNode.cpp
    src/rendering/nodes/SceneNode.cpp
    src/rendering/nodes/PickingNode.cpp
    src/rendering/nodes/GBufferNode.cpp
    src/rendering/nodes/ForwardRenderNode.cpp
    src/rendering/nodes/SlowForwardRenderNode.cpp
    src/rendering/nodes/ShadowMapNode.cpp
    src/rendering/nodes/SkyViewNode.cpp
    src/rendering/nodes/RTAccelerationStructures.cpp
    src/rendering/nodes/RTFirstHitNode.cpp
    src/rendering/nodes/RTReflectionsNode.cpp
    src/rendering/nodes/RTDiffuseGINode.cpp
    src/rendering/nodes/RTAmbientOcclusion.cpp
    src/utility/GlobalState.cpp
    src/utility/Input.cpp
    src/utility/Image.cpp
    src/utility/FileIO.cpp)

target_include_directories(ArkoseRenderer PRIVATE src/)
target_include_directories(ArkoseRenderer PRIVATE shaders/shared)

# (C++20 is required for designated initializers in VC++)
target_compile_features(ArkoseRenderer PRIVATE cxx_std_20)

if (MSVC)
    # Both of these seem to be required for the VulkanSDK prebuilt libraries
    # Probably it's just something I haven't figured out with this..
    #add_compile_options(/MT)
    #target_compile_options(ArkoseRenderer PRIVATE /MT)

    target_compile_definitions(ArkoseRenderer PRIVATE NOMINMAX)
    #target_compile_options(ArkoseRenderer PRIVATE /Wall)
else()
    target_compile_options(ArkoseRenderer PRIVATE -Wall)
    target_compile_options(ArkoseRenderer PRIVATE -Wimplicit-fallthrough)
endif()

FetchContent_Declare(mooslib GIT_REPOSITORY https://github.com/Shimmen/mooslib.git)
FetchContent_GetProperties(mooslib)
if(NOT mooslib_POPULATED)
    FetchContent_Populate(mooslib)
    set(MOOSLIB_BuildTests OFF)
    add_subdirectory(${mooslib_SOURCE_DIR} ${mooslib_BINARY_DIR})
endif()
target_link_libraries(ArkoseRenderer PRIVATE mooslib)

FetchContent_Declare(json
    GIT_REPOSITORY https://github.com/ArthurSonzogni/nlohmann_json_cmake_fetchcontent.git
    GIT_TAG v3.7.3)
FetchContent_GetProperties(json)
if(NOT json_POPULATED)
    FetchContent_Populate(json)
    add_subdirectory(${json_SOURCE_DIR} ${json_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()
target_link_libraries(ArkoseRenderer PRIVATE nlohmann_json::nlohmann_json)

FetchContent_Declare(fmt
        GIT_REPOSITORY https://github.com/fmtlib/fmt.git
        GIT_TAG 7.0.1)
FetchContent_GetProperties(fmt)
if(NOT fmt_POPULATED)
    FetchContent_Populate(fmt)
    add_subdirectory(${fmt_SOURCE_DIR} ${fmt_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()
target_link_libraries(ArkoseRenderer PRIVATE fmt)

FetchContent_Declare(spirvcross
    GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Cross.git
    GIT_TAG        "2020-02-26")
FetchContent_GetProperties(spirvcross)
if(NOT spirvcross_POPULATED)
    FetchContent_Populate(spirvcross)
    set(SPIRV_CROSS_CLI OFF)
    add_subdirectory(${spirvcross_SOURCE_DIR} ${spirvcross_BINARY_DIR})
endif()
target_link_libraries(ArkoseRenderer PRIVATE spirv-cross-core spirv-cross-glsl spirv-cross-reflect)

FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        "3.3-stable")
FetchContent_GetProperties(glfw)
if(NOT glfw_POPULATED)
    FetchContent_Populate(glfw)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    add_subdirectory(${glfw_SOURCE_DIR} ${glfw_BINARY_DIR})
endif()
target_link_libraries(ArkoseRenderer PRIVATE glfw)

add_subdirectory(deps/stb_image)
target_link_libraries(ArkoseRenderer PRIVATE stb_image)

add_subdirectory(deps/tiny_gltf)
target_link_libraries(ArkoseRenderer PRIVATE tiny_gltf)

find_package(Vulkan REQUIRED)
target_link_libraries(ArkoseRenderer PRIVATE Vulkan::Vulkan)
if (WIN32)
    target_compile_definitions(ArkoseRenderer PRIVATE VK_USE_PLATFORM_WIN32_KHR)
endif ()

find_package(Shaderc REQUIRED)
target_link_libraries(ArkoseRenderer PRIVATE Shaderc::Shaderc)

add_subdirectory(deps/vulkan_memory_allocator)
target_link_libraries(ArkoseRenderer PRIVATE vulkan_memory_allocator)

add_subdirectory(deps/dear-imgui)
target_link_libraries(ArkoseRenderer PRIVATE dear_imgui)

add_subdirectory(deps/half)
target_link_libraries(ArkoseRenderer PRIVATE half)
