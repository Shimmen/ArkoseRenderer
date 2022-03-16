cmake_minimum_required(VERSION 3.8 FATAL_ERROR)
project(ArkoseRenderer)

include(FetchContent)

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})

add_subdirectory(deps/tracy)
add_executable(ArkoseRenderer
    ${TRACY_CLIENT_SOURCE}
    src/main.cpp
    src/apps/ShowcaseApp.cpp
    src/backend/Resource.cpp
    src/backend/shader/Shader.cpp
    src/backend/shader/ShaderFile.cpp
    src/backend/shader/ShaderManager.cpp
    src/backend/base/AccelerationStructure.cpp
    src/backend/base/Backend.cpp
    src/backend/base/BindingSet.cpp
    src/backend/base/Buffer.cpp
    src/backend/base/ComputeState.cpp
    src/backend/base/RayTracingState.cpp
    src/backend/base/RenderState.cpp
    src/backend/base/RenderTarget.cpp
    src/backend/base/Texture.cpp
    src/core/parallel/TaskGraph.cpp
    src/backend/util/StateBindings.cpp
    src/backend/util/UploadBuffer.cpp
    src/backend/vulkan/VulkanBackend.cpp
    src/backend/vulkan/VulkanBindingSet.cpp
    src/backend/vulkan/VulkanBuffer.cpp
    src/backend/vulkan/VulkanCommandList.cpp
    src/backend/vulkan/VulkanComputeState.cpp
    src/backend/vulkan/VulkanRenderState.cpp
    src/backend/vulkan/VulkanRenderTarget.cpp
    src/backend/vulkan/VulkanTexture.cpp
    src/backend/vulkan/extensions/debug-utils/VulkanDebugUtils.cpp
    src/backend/vulkan/extensions/ray-tracing-khr/VulkanRayTracingKHR.cpp
    src/backend/vulkan/extensions/ray-tracing-khr/VulkanAccelerationStructureKHR.cpp
    src/backend/vulkan/extensions/ray-tracing-khr/VulkanRayTracingStateKHR.cpp
    src/backend/vulkan/extensions/ray-tracing-nv/VulkanRayTracingNV.cpp
    src/backend/vulkan/extensions/ray-tracing-nv/VulkanAccelerationStructureNV.cpp
    src/backend/vulkan/extensions/ray-tracing-nv/VulkanRayTracingStateNV.cpp
    src/math/Fibonacci.cpp
    src/math/Frustum.cpp
    src/math/Halton.cpp
    src/rendering/camera/Camera.cpp
    src/rendering/camera/FpsCamera.cpp
    src/rendering/nodes/BloomNode.cpp
    src/rendering/nodes/CullingNode.cpp
    src/rendering/nodes/DDGINode.cpp
    src/rendering/nodes/DDGIProbeDebug.cpp
    src/rendering/nodes/ForwardRenderNode.cpp
    src/rendering/nodes/FinalNode.cpp
    src/rendering/nodes/FXAANode.cpp
    src/rendering/nodes/GIComposeNode.cpp
    src/rendering/nodes/PickingNode.cpp
    src/rendering/nodes/PrepassNode.cpp
    src/rendering/nodes/RTDirectLightNode.cpp
    src/rendering/nodes/RTFirstHitNode.cpp
    src/rendering/nodes/RTReflectionsNode.cpp
    src/rendering/nodes/ShadowMapNode.cpp
    src/rendering/nodes/SkyViewNode.cpp
    src/rendering/nodes/SSAONode.cpp
    src/rendering/nodes/TAANode.cpp
    src/rendering/nodes/TonemapNode.cpp
    src/rendering/Registry.cpp
    src/rendering/RenderPipeline.cpp
    src/rendering/RenderPipelineNode.cpp
    src/rendering/scene/lights/Light.cpp
    src/rendering/scene/lights/DirectionalLight.cpp
    src/rendering/scene/lights/SpotLight.cpp
    src/rendering/scene/Material.cpp
    src/rendering/scene/Mesh.cpp
    src/rendering/scene/Model.cpp
    src/rendering/scene/models/GltfModel.cpp
    src/rendering/scene/ProbeGrid.cpp
    src/rendering/scene/Scene.cpp
    src/rendering/scene/SceneSerialization.cpp
    src/rendering/scene/Vertex.cpp
    src/utility/AvgElapsedTimer.cpp
    src/utility/FileIO.cpp
    src/utility/IESProfile.cpp
    src/utility/Image.cpp
    src/utility/Input.cpp
    )

target_include_directories(ArkoseRenderer PRIVATE src/)
target_include_directories(ArkoseRenderer PRIVATE shaders/shared)

target_link_libraries(ArkoseRenderer PUBLIC tracy)

##################################
######## BUILD SETTINGS  #########
##################################

set_property(TARGET ArkoseRenderer PROPERTY CXX_STANDARD 20)

# Enable tracy real-time debugger? It does leak memory when enabled, so maybe keep off by default?
target_compile_definitions(ArkoseRenderer PUBLIC TRACY_ENABLE)

if (MSVC)
    target_compile_options(ArkoseRenderer PUBLIC
        /wd26812 # prefer scoped enum class
        )
    target_compile_definitions(ArkoseRenderer PRIVATE
        NOMINMAX _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(ArkoseRenderer PRIVATE -Wall)
    target_compile_options(ArkoseRenderer PRIVATE -Wimplicit-fallthrough)
endif()

##################################
#### CUSTOM BUILD STEPS ETC. #####
##################################

set(ARKOSE_ASSET_DIR_NAME "assets")
set(ARKOSE_SHADER_DIR_NAME "shaders")

add_custom_command(TARGET ArkoseRenderer PRE_BUILD
    COMMAND ${CMAKE_COMMAND} -E create_symlink "${CMAKE_SOURCE_DIR}/${ARKOSE_ASSET_DIR_NAME}" "${CMAKE_BINARY_DIR}/${ARKOSE_ASSET_DIR_NAME}"
    COMMAND ${CMAKE_COMMAND} -E create_symlink "${CMAKE_SOURCE_DIR}/${ARKOSE_SHADER_DIR_NAME}" "${CMAKE_BINARY_DIR}/${ARKOSE_SHADER_DIR_NAME}"
    COMMENT "Create symlinks to the assets & shaders so they will be locatable relative to the executable"
    VERBATIM)

##################################
#### THIRD-PARTY DEPENDENCIES ####
##################################

##################################
# VulkanSDK dependencies

find_package(Vulkan REQUIRED)
target_link_libraries(ArkoseRenderer PRIVATE Vulkan::Vulkan)

find_package(Shaderc REQUIRED)
target_link_libraries(ArkoseRenderer PRIVATE Shaderc::Shaderc)

find_package(SpirvCross REQUIRED)
target_link_libraries(ArkoseRenderer PRIVATE SpirvCross::SpirvCross)

##################################
# Downloaded & built dependencies

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

##################################
# In-tree built dependencies

add_subdirectory(deps/stb_image)
target_link_libraries(ArkoseRenderer PRIVATE stb_image)

add_subdirectory(deps/tiny_gltf)
target_link_libraries(ArkoseRenderer PRIVATE tiny_gltf)

add_subdirectory(deps/vulkan_memory_allocator)
target_link_libraries(ArkoseRenderer PRIVATE vulkan_memory_allocator)

add_subdirectory(deps/dear-imgui)
target_link_libraries(ArkoseRenderer PRIVATE dear_imgui)

add_subdirectory(deps/half)
target_link_libraries(ArkoseRenderer PRIVATE half)
