cmake_minimum_required(VERSION 3.15 FATAL_ERROR)

# "MSVC runtime library flags are selected by an abstraction."
# See https://cmake.org/cmake/help/latest/policy/CMP0091.html
cmake_policy(SET CMP0091 NEW)

project(Arkose)

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})

# Set default c++ version
set(CMAKE_CXX_STANDARD 20)

##################################
######### PLATFORM SETUP #########
##################################

set(ARKOSE_BACKEND_VULKAN false)
set(ARKOSE_BACKEND_D3D12  false)

if (WIN32)
    set(ARKOSE_BACKEND_VULKAN true)
    set(ARKOSE_BACKEND_D3D12  true)
elseif (APPLE)
    set(ARKOSE_BACKEND_VULKAN true)
else()
    message(SEND_ERROR "Platform unsupported")
endif()

##################################
#### THIRD-PARTY DEPENDENCIES ####
##################################

##################################
# VulkanSDK dependencies

if (ARKOSE_BACKEND_VULKAN)
    find_package(Vulkan REQUIRED)
    find_package(Shaderc REQUIRED) # TODO: Provide alternate route for Shaderc? Or just require Vulkan backend..
endif()

##################################
# In-tree built dependencies

add_subdirectory(deps)
add_subdirectory(deps/arklib)

##################################
########## ARKOSE CORE ###########
##################################

add_library(ArkoseCore STATIC
    arkcore/asset/AnimationAsset.cpp
    arkcore/asset/ImageAsset.cpp
    arkcore/asset/LevelAsset.cpp
    arkcore/asset/MaterialAsset.cpp
    arkcore/asset/SkeletonAsset.cpp
    arkcore/asset/MeshAsset.cpp
    arkcore/asset/TextureCompressor.cpp
    arkcore/asset/import/AssetImporter.cpp
    arkcore/asset/import/GltfLoader.cpp
    arkcore/core/math/Fibonacci.cpp
    arkcore/core/math/Frustum.cpp
    arkcore/core/math/Halton.cpp
    arkcore/core/parallel/Task.cpp
    arkcore/core/parallel/TaskGraph.cpp
    arkcore/scene/ProbeGrid.cpp
    arkcore/scene/Transform.cpp
    arkcore/scene/Vertex.cpp
    arkcore/utility/FileDialog.cpp
    arkcore/utility/FileIO.cpp
    )

target_include_directories(ArkoseCore PUBLIC arkcore/)

get_target_property(ARKOCORE_SOURCES ArkoseCore SOURCES)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/arkcore" FILES ${ARKOCORE_SOURCES})

# todo: see if we can move some of the libraries to PRIVATE
target_link_libraries(ArkoseCore PUBLIC
    arklib # core arkose library (math etc.)

    fmt # string formatting (TODO: replace with stl fmt in C++20)
    half # 16-bit float
    TracyClient # real-time profiling
    concurrentqueue # lock-free concurrent queue

    glfw # window and event management
    nfd # native file dialogs

    # Asset related
    stb_image # image loading
    tiny_gltf # glTF 2.0 loading
    cereal # serialisation
    lz4_static # generic lossless compression
    bc7enc_lib # texture compression
    meshoptimizer # mesh optimization & meshlet generation
    )

##################################
######## ARKOSE RENDERER #########
##################################

add_executable(ArkoseRenderer
    arkose/main.cpp
    arkose/animation/Animation.cpp
    arkose/apps/BootstrappingApp.cpp
    arkose/apps/MeshViewerApp.cpp
    arkose/apps/ShowcaseApp.cpp
    arkose/apps/SSSDemo.cpp
    arkose/input/Input.cpp
    arkose/physics/PhysicsScene.cpp
    arkose/physics/backend/base/PhysicsBackend.cpp
    arkose/physics/backend/jolt/JoltPhysicsBackend.cpp
    arkose/physics/backend/jolt/JoltVisualiser.cpp
    arkose/rendering/DrawKey.cpp
    arkose/rendering/GpuScene.cpp
    arkose/rendering/Icon.cpp
    arkose/rendering/IconManager.cpp
    arkose/rendering/Registry.cpp
    arkose/rendering/RenderPipeline.cpp
    arkose/rendering/RenderPipelineNode.cpp
    arkose/rendering/SkeletalMesh.cpp
    arkose/rendering/Skeleton.cpp
    arkose/rendering/StaticMesh.cpp
    arkose/rendering/VertexManager.cpp
    arkose/rendering/backend/Resource.cpp
    arkose/rendering/backend/shader/Shader.cpp
    arkose/rendering/backend/shader/ShaderFile.cpp
    arkose/rendering/backend/shader/ShaderManager.cpp
    arkose/rendering/backend/base/AccelerationStructure.cpp
    arkose/rendering/backend/base/Backend.cpp
    arkose/rendering/backend/base/BindingSet.cpp
    arkose/rendering/backend/base/Buffer.cpp
    arkose/rendering/backend/base/ComputeState.cpp
    arkose/rendering/backend/base/RayTracingState.cpp
    arkose/rendering/backend/base/RenderState.cpp
    arkose/rendering/backend/base/RenderTarget.cpp
    arkose/rendering/backend/base/Texture.cpp
    arkose/rendering/backend/base/UpscalingState.cpp
    arkose/rendering/backend/util/StateBindings.cpp
    arkose/rendering/backend/util/UploadBuffer.cpp
    arkose/rendering/debug/DebugDrawer.cpp
    arkose/rendering/forward/ForwardRenderNode.cpp
    arkose/rendering/forward/PrepassNode.cpp
    arkose/rendering/lighting/LightingComposeNode.cpp
    arkose/rendering/meshlet/MeshletCuller.cpp
    arkose/rendering/meshlet/MeshletManager.cpp
    arkose/rendering/meshlet/MeshletDebugNode.cpp
    arkose/rendering/meshlet/MeshletForwardRenderNode.cpp
    arkose/rendering/meshlet/MeshletIndirectHelper.cpp
    arkose/rendering/meshlet/MeshletVisibilityBufferRenderNode.cpp
    arkose/rendering/meshlet/VisibilityBufferDebugNode.cpp
    arkose/rendering/nodes/BloomNode.cpp
    arkose/rendering/nodes/DDGINode.cpp
    arkose/rendering/nodes/DDGIProbeDebug.cpp
    arkose/rendering/nodes/DebugDrawNode.cpp
    arkose/rendering/nodes/DepthOfFieldNode.cpp
    arkose/rendering/nodes/DirectionalLightShadowNode.cpp
    arkose/rendering/nodes/FinalNode.cpp
    arkose/rendering/nodes/FXAANode.cpp
    arkose/rendering/nodes/LocalLightShadowNode.cpp
    arkose/rendering/nodes/PickingNode.cpp
    arkose/rendering/nodes/RTVisualisationNode.cpp
    arkose/rendering/nodes/RTSphereLightShadowNode.cpp
    arkose/rendering/nodes/RTReflectionsNode.cpp
    arkose/rendering/nodes/SkyViewNode.cpp
    arkose/rendering/nodes/SSAONode.cpp
    arkose/rendering/nodes/TAANode.cpp
    arkose/rendering/nodes/TonemapNode.cpp
    arkose/rendering/nodes/VisibilityBufferShadingNode.cpp
    arkose/rendering/postprocess/CASNode.cpp
    arkose/rendering/postprocess/SSSSNode.cpp
    arkose/rendering/upscaling/UpscalingNode.cpp
    arkose/scene/MeshInstance.cpp
    arkose/scene/Scene.cpp
    arkose/scene/camera/Camera.cpp
    arkose/scene/camera/CameraController.cpp
    arkose/scene/camera/FpsCameraController.cpp
    arkose/scene/editor/EditorGizmo.cpp
    arkose/scene/lights/DirectionalLight.cpp
    arkose/scene/lights/IESProfile.cpp
    arkose/scene/lights/Light.cpp
    arkose/scene/lights/LightAttenuation.cpp
    arkose/scene/lights/SphereLight.cpp
    arkose/scene/lights/SpotLight.cpp
    arkose/utility/AvgElapsedTimer.cpp
    )

if (ARKOSE_BACKEND_VULKAN)
    target_compile_definitions(ArkoseRenderer PRIVATE WITH_VULKAN)
    target_sources(ArkoseRenderer PRIVATE
        arkose/rendering/backend/vulkan/VulkanBackend.cpp
        arkose/rendering/backend/vulkan/VulkanBindingSet.cpp
        arkose/rendering/backend/vulkan/VulkanBuffer.cpp
        arkose/rendering/backend/vulkan/VulkanCommandList.cpp
        arkose/rendering/backend/vulkan/VulkanComputeState.cpp
        arkose/rendering/backend/vulkan/VulkanRenderState.cpp
        arkose/rendering/backend/vulkan/VulkanRenderTarget.cpp
        arkose/rendering/backend/vulkan/VulkanTexture.cpp
        arkose/rendering/backend/vulkan/VulkanUpscalingState.cpp
        arkose/rendering/backend/vulkan/extensions/debug-utils/VulkanDebugUtils.cpp
        arkose/rendering/backend/vulkan/extensions/mesh-shader-ext/VulkanMeshShaderEXT.cpp
        arkose/rendering/backend/vulkan/extensions/ray-tracing-khr/VulkanRayTracingKHR.cpp
        arkose/rendering/backend/vulkan/extensions/ray-tracing-khr/VulkanAccelerationStructureKHR.cpp
        arkose/rendering/backend/vulkan/extensions/ray-tracing-khr/VulkanRayTracingStateKHR.cpp
        arkose/rendering/backend/vulkan/extensions/ray-tracing-nv/VulkanRayTracingNV.cpp
        arkose/rendering/backend/vulkan/extensions/ray-tracing-nv/VulkanAccelerationStructureNV.cpp
        arkose/rendering/backend/vulkan/extensions/ray-tracing-nv/VulkanRayTracingStateNV.cpp
        )
    target_link_libraries(ArkoseRenderer PRIVATE
        Vulkan::Vulkan
        VulkanMemoryAllocator
        )
endif()

if (ARKOSE_BACKEND_D3D12)
    target_compile_definitions(ArkoseRenderer PRIVATE WITH_D3D12)
    target_sources(ArkoseRenderer PRIVATE
        arkose/rendering/backend/d3d12/D3D12Backend.cpp
        arkose/rendering/backend/d3d12/D3D12BindingSet.cpp
        arkose/rendering/backend/d3d12/D3D12Buffer.cpp
        arkose/rendering/backend/d3d12/D3D12CommandList.cpp
        arkose/rendering/backend/d3d12/D3D12ComputeState.cpp
        arkose/rendering/backend/d3d12/D3D12RenderState.cpp
        arkose/rendering/backend/d3d12/D3D12RenderTarget.cpp
        arkose/rendering/backend/d3d12/D3D12Texture.cpp
        )
    target_link_libraries(ArkoseRenderer PRIVATE
        d3d12 dxgi dxguid  # core stuff
        dxcompiler # for HLSL shader compilation
        DirectX-Headers # utility stuff, e.g. CD3DX12
        spirv-cross-hlsl # for spirv->hlsl transpiling
        )
endif()

if(MSVC)
    target_sources(ArkoseRenderer PUBLIC "arkose/Arkose.natvis")
endif()

target_include_directories(ArkoseRenderer PRIVATE arkose/)

get_target_property(ARKOSE_SOURCES ArkoseRenderer SOURCES)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/arkose" FILES ${ARKOSE_SOURCES})

target_link_libraries(ArkoseRenderer PRIVATE
    ArkoseCore # core arkose library

    dear_imgui # dear imgui - immediate mode gui
    imguizmo # gizmo extension for dear imgui
    implot # plot extension for dear imgui

    # Physics
    Jolt

    # Shader backend
    Shaderc::Shaderc # glsl shader compiler
    spirv-cross-core # generic spir-v reflection
    )

if(DLSS_AVAILABLE)
    message(STATUS "ArkoseRenderer: building with DLSS support")
    target_link_libraries(ArkoseRenderer PRIVATE dlss)
    target_compile_definitions(ArkoseRenderer PUBLIC WITH_DLSS=1)
    target_sources(ArkoseRenderer PRIVATE arkose/rendering/backend/vulkan/features/dlss/VulkanDLSS.cpp)
else()
    message(STATUS "ArkoseRenderer: building without DLSS support")
endif()

add_custom_command(TARGET ArkoseRenderer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy -t $<TARGET_FILE_DIR:ArkoseRenderer> $<TARGET_RUNTIME_DLLS:ArkoseRenderer>
    COMMAND_EXPAND_LISTS
)

##################################
######## BUILD SETTINGS  #########
##################################

set_property(TARGET ArkoseRenderer PROPERTY CXX_STANDARD 20)

if (MSVC)
    # Ensure we're using the dynamically linked runtime library for MSVC.
    # When linking with Tracy this is required, as it will dynamically load dlls.
    set(MSVC_RUNTIME_LIBRARY_STRING "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    set_property(TARGET ArkoseRenderer PROPERTY MSVC_RUNTIME_LIBRARY ${MSVC_RUNTIME_LIBRARY_STRING})
    set_property(TARGET Jolt PROPERTY MSVC_RUNTIME_LIBRARY ${MSVC_RUNTIME_LIBRARY_STRING})

    # Avoid including too much noise from windows.h
    target_compile_definitions(ArkoseRenderer PUBLIC WIN32_LEAN_AND_MEAN)

    # Explicitly set _ITERATOR_DEBUG_LEVEL to 2 for debug builds so it's clear what we're using.
    # Level 2 is the default level for MSVC Debug builds and will be compatible with most prebuilt libraries.
    add_compile_definitions($<$<CONFIG:Debug>:_ITERATOR_DEBUG_LEVEL=2>)
endif()

# Keep tracy enabled at all time but with on-demand only, meaning we don't accumulate tracy data
# in the Arkose process which can hog a lot of memory, which looks a lot like a memory leak.
target_compile_definitions(ArkoseRenderer PUBLIC TRACY_ENABLE)
# Disable on-demand for now as the latest version seems to not work correctly with it
#target_compile_definitions(ArkoseRenderer PUBLIC TRACY_ON_DEMAND)

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

# Make assets & shaders accessible in runtime relative to the exe-location

add_custom_command(TARGET ArkoseRenderer PRE_BUILD
    COMMAND ${CMAKE_COMMAND} -E create_symlink "${CMAKE_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:ArkoseRenderer>/assets"
    COMMAND ${CMAKE_COMMAND} -E create_symlink "${CMAKE_SOURCE_DIR}/arkose/shaders" "$<TARGET_FILE_DIR:ArkoseRenderer>/shaders"
    COMMENT "Create symlinks to the assets & shaders so they will be locatable relative to the executable"
    VERBATIM)

##################################
######### ARKOSE TOOLING #########
##################################

set(ARKOSE_BUILD_TOOLS True)

if((DEFINED {PXR_USD_LOCATION}) OR (DEFINED ENV{PXR_USD_LOCATION}))
    message(STATUS "Will attempt to build tools")
else()
    message(STATUS "Will NOT attempt to build tools as PXR_USD_LOCATION is not set")
    set(ARKOSE_BUILD_TOOLS False)
endif()

if(ARKOSE_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

##################################
