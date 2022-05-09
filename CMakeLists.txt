cmake_minimum_required(VERSION 3.8 FATAL_ERROR)
project(ArkoseRenderer)

include(FetchContent)

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})

add_subdirectory(deps/tracy)
add_executable(ArkoseRenderer
    ${TRACY_CLIENT_SOURCE}
    arkose/main.cpp
    arkose/apps/ShowcaseApp.cpp
    arkose/backend/Resource.cpp
    arkose/backend/shader/Shader.cpp
    arkose/backend/shader/ShaderFile.cpp
    arkose/backend/shader/ShaderManager.cpp
    arkose/backend/base/AccelerationStructure.cpp
    arkose/backend/base/Backend.cpp
    arkose/backend/base/BindingSet.cpp
    arkose/backend/base/Buffer.cpp
    arkose/backend/base/ComputeState.cpp
    arkose/backend/base/RayTracingState.cpp
    arkose/backend/base/RenderState.cpp
    arkose/backend/base/RenderTarget.cpp
    arkose/backend/base/Texture.cpp
    arkose/core/parallel/TaskGraph.cpp
    arkose/backend/util/StateBindings.cpp
    arkose/backend/util/UploadBuffer.cpp
    arkose/backend/vulkan/VulkanBackend.cpp
    arkose/backend/vulkan/VulkanBindingSet.cpp
    arkose/backend/vulkan/VulkanBuffer.cpp
    arkose/backend/vulkan/VulkanCommandList.cpp
    arkose/backend/vulkan/VulkanComputeState.cpp
    arkose/backend/vulkan/VulkanRenderState.cpp
    arkose/backend/vulkan/VulkanRenderTarget.cpp
    arkose/backend/vulkan/VulkanTexture.cpp
    arkose/backend/vulkan/extensions/debug-utils/VulkanDebugUtils.cpp
    arkose/backend/vulkan/extensions/ray-tracing-khr/VulkanRayTracingKHR.cpp
    arkose/backend/vulkan/extensions/ray-tracing-khr/VulkanAccelerationStructureKHR.cpp
    arkose/backend/vulkan/extensions/ray-tracing-khr/VulkanRayTracingStateKHR.cpp
    arkose/backend/vulkan/extensions/ray-tracing-nv/VulkanRayTracingNV.cpp
    arkose/backend/vulkan/extensions/ray-tracing-nv/VulkanAccelerationStructureNV.cpp
    arkose/backend/vulkan/extensions/ray-tracing-nv/VulkanRayTracingStateNV.cpp
    arkose/math/Fibonacci.cpp
    arkose/math/Frustum.cpp
    arkose/math/Halton.cpp
    arkose/rendering/camera/Camera.cpp
    arkose/rendering/camera/FpsCamera.cpp
    arkose/rendering/nodes/BloomNode.cpp
    arkose/rendering/nodes/CullingNode.cpp
    arkose/rendering/nodes/DDGINode.cpp
    arkose/rendering/nodes/DDGIProbeDebug.cpp
    arkose/rendering/nodes/ForwardRenderNode.cpp
    arkose/rendering/nodes/FinalNode.cpp
    arkose/rendering/nodes/FXAANode.cpp
    arkose/rendering/nodes/GIComposeNode.cpp
    arkose/rendering/nodes/PickingNode.cpp
    arkose/rendering/nodes/PrepassNode.cpp
    arkose/rendering/nodes/RTDirectLightNode.cpp
    arkose/rendering/nodes/RTFirstHitNode.cpp
    arkose/rendering/nodes/RTReflectionsNode.cpp
    arkose/rendering/nodes/ShadowMapNode.cpp
    arkose/rendering/nodes/SkyViewNode.cpp
    arkose/rendering/nodes/SSAONode.cpp
    arkose/rendering/nodes/TAANode.cpp
    arkose/rendering/nodes/TonemapNode.cpp
    arkose/rendering/Registry.cpp
    arkose/rendering/RenderPipeline.cpp
    arkose/rendering/RenderPipelineNode.cpp
    arkose/rendering/scene/lights/Light.cpp
    arkose/rendering/scene/lights/DirectionalLight.cpp
    arkose/rendering/scene/lights/SpotLight.cpp
    arkose/rendering/scene/Material.cpp
    arkose/rendering/scene/Mesh.cpp
    arkose/rendering/scene/Model.cpp
    arkose/rendering/scene/models/GltfModel.cpp
    arkose/rendering/scene/ProbeGrid.cpp
    arkose/rendering/scene/Scene.cpp
    arkose/rendering/scene/GpuScene.cpp
    arkose/rendering/scene/Vertex.cpp
    arkose/utility/AvgElapsedTimer.cpp
    arkose/utility/FileIO.cpp
    arkose/utility/IESProfile.cpp
    arkose/utility/Image.cpp
    arkose/utility/Input.cpp
    )

target_include_directories(ArkoseRenderer PRIVATE arkose/)
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
        GIT_TAG 8.1.1)
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

FetchContent_Declare(dearimgui_fetch
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        ec48681455694c85982861dfa3f304fd544d3137) # NOTE: This is the docking branch!
FetchContent_GetProperties(dearimgui_fetch)
if(NOT dearimgui_fetch_POPULATED)
    FetchContent_Populate(dearimgui_fetch)

    add_library(dear_imgui
        "${dearimgui_fetch_SOURCE_DIR}/imgui.cpp"
        "${dearimgui_fetch_SOURCE_DIR}/imgui_draw.cpp"
        "${dearimgui_fetch_SOURCE_DIR}/imgui_widgets.cpp"
        "${dearimgui_fetch_SOURCE_DIR}/imgui_tables.cpp"
        "${dearimgui_fetch_SOURCE_DIR}/imgui_demo.cpp"
        "${dearimgui_fetch_SOURCE_DIR}/backends/imgui_impl_glfw.cpp"
        "${dearimgui_fetch_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp") # TODO: What about non-vulkan builds?

    target_compile_features(dear_imgui PRIVATE cxx_std_17)
    target_include_directories(dear_imgui PUBLIC ${dearimgui_fetch_SOURCE_DIR})

    target_link_libraries(dear_imgui glfw)

    target_include_directories(dear_imgui PRIVATE Vulkan::Vulkan)
    target_link_libraries(dear_imgui Vulkan::Vulkan)

endif()
target_link_libraries(ArkoseRenderer PRIVATE dear_imgui)

FetchContent_Declare(imguizmo_fetch
    GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo.git
    GIT_TAG        8afa1d7050392111cb917a6439dc76450cda5aa6)
FetchContent_GetProperties(imguizmo_fetch)
if(NOT imguizmo_fetch_POPULATED)
    FetchContent_Populate(imguizmo_fetch)

    add_library(imguizmo
        "${imguizmo_fetch_SOURCE_DIR}/ImGuizmo.cpp"
        # NOTE: More files are available part of this repo
        )

    target_compile_features(imguizmo PRIVATE cxx_std_17)
    target_include_directories(imguizmo PUBLIC ${imguizmo_fetch_SOURCE_DIR})
    target_link_libraries(imguizmo PRIVATE dear_imgui)

endif()
target_link_libraries(ArkoseRenderer PRIVATE imguizmo)

FetchContent_Declare(vulkan_memory_allocator
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG        "v3.0.0")
FetchContent_GetProperties(vulkan_memory_allocator)
if(NOT vulkan_memory_allocator_POPULATED)
    FetchContent_Populate(vulkan_memory_allocator)
    add_subdirectory(${vulkan_memory_allocator_SOURCE_DIR} ${vulkan_memory_allocator_BINARY_DIR})
endif()
target_link_libraries(ArkoseRenderer PRIVATE VulkanMemoryAllocator)

##################################
# In-tree built dependencies

add_subdirectory(deps/stb_image)
target_link_libraries(ArkoseRenderer PRIVATE stb_image)

add_subdirectory(deps/tiny_gltf)
target_link_libraries(ArkoseRenderer PRIVATE tiny_gltf)

add_subdirectory(deps/half)
target_link_libraries(ArkoseRenderer PRIVATE half)
