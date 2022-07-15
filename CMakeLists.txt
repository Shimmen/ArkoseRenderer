cmake_minimum_required(VERSION 3.8 FATAL_ERROR)

# "MSVC runtime library flags are selected by an abstraction."
# See https://cmake.org/cmake/help/latest/policy/CMP0091.html
cmake_policy(SET CMP0091 NEW)

project(ArkoseRenderer)

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})

##################################
#### THIRD-PARTY DEPENDENCIES ####
##################################

##################################
# VulkanSDK dependencies

find_package(Vulkan REQUIRED)
find_package(Shaderc REQUIRED)

##################################
# In-tree built dependencies

add_subdirectory(deps)
add_subdirectory(arklib)

##################################
###### MAIN TARGET: ARKOSE #######
##################################

add_executable(ArkoseRenderer
    ${TRACY_CLIENT_SOURCE}
    arkose/main.cpp
    arkose/apps/BootstrappingApp.cpp
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
    arkose/backend/util/StateBindings.cpp
    arkose/backend/util/UploadBuffer.cpp
    arkose/backend/d3d12/D3D12Backend.cpp
    arkose/backend/d3d12/D3D12BindingSet.cpp
    arkose/backend/d3d12/D3D12Buffer.cpp
    arkose/backend/d3d12/D3D12CommandList.cpp
    arkose/backend/d3d12/D3D12ComputeState.cpp
    arkose/backend/d3d12/D3D12RenderState.cpp
    arkose/backend/d3d12/D3D12RenderTarget.cpp
    arkose/backend/d3d12/D3D12Texture.cpp
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
    arkose/core/math/Fibonacci.cpp
    arkose/core/math/Frustum.cpp
    arkose/core/math/Halton.cpp
    arkose/core/parallel/TaskGraph.cpp
    arkose/physics/PhysicsScene.cpp
    arkose/physics/backend/base/PhysicsBackend.cpp
    arkose/physics/backend/jolt/JoltPhysicsBackend.cpp
    arkose/physics/backend/jolt/JoltVisualiser.cpp
    arkose/rendering/GpuScene.cpp
    arkose/rendering/Registry.cpp
    arkose/rendering/RenderPipeline.cpp
    arkose/rendering/RenderPipelineNode.cpp
    arkose/rendering/nodes/BloomNode.cpp
    arkose/rendering/nodes/CullingNode.cpp
    arkose/rendering/nodes/DDGINode.cpp
    arkose/rendering/nodes/DDGIProbeDebug.cpp
    arkose/rendering/nodes/DepthOfFieldNode.cpp
    arkose/rendering/nodes/DirectionalLightShadowNode.cpp
    arkose/rendering/nodes/ForwardRenderNode.cpp
    arkose/rendering/nodes/FinalNode.cpp
    arkose/rendering/nodes/FXAANode.cpp
    arkose/rendering/nodes/GIComposeNode.cpp
    arkose/rendering/nodes/LocalLightShadowNode.cpp
    arkose/rendering/nodes/PickingNode.cpp
    arkose/rendering/nodes/PrepassNode.cpp
    arkose/rendering/nodes/RTDirectLightNode.cpp
    arkose/rendering/nodes/RTFirstHitNode.cpp
    arkose/rendering/nodes/RTReflectionsNode.cpp
    arkose/rendering/nodes/SkyViewNode.cpp
    arkose/rendering/nodes/SSAONode.cpp
    arkose/rendering/nodes/TAANode.cpp
    arkose/rendering/nodes/TonemapNode.cpp
    arkose/scene/Material.cpp
    arkose/scene/Mesh.cpp
    arkose/scene/Model.cpp
    arkose/scene/ProbeGrid.cpp
    arkose/scene/Scene.cpp
    arkose/scene/Vertex.cpp
    arkose/scene/camera/Camera.cpp
    arkose/scene/camera/CameraController.cpp
    arkose/scene/camera/FpsCameraController.cpp
    arkose/scene/lights/DirectionalLight.cpp
    arkose/scene/lights/SpotLight.cpp
    arkose/scene/models/GltfModel.cpp
    arkose/utility/AvgElapsedTimer.cpp
    arkose/utility/FileIO.cpp
    arkose/utility/IESProfile.cpp
    arkose/utility/Image.cpp
    arkose/utility/Input.cpp
    )

target_include_directories(ArkoseRenderer PRIVATE arkose/)
target_include_directories(ArkoseRenderer PRIVATE shaders/shared)

target_link_libraries(ArkoseRenderer PRIVATE
    arklib
    dear_imgui
    fmt
    glfw
    half
    imguizmo
    nlohmann_json::nlohmann_json
    Shaderc::Shaderc
    spirv-cross-core
    stb_image
    tiny_gltf
    tracy

    # Physics
    Jolt

    # Vulkan backend
    Vulkan::Vulkan
    VulkanMemoryAllocator

    # D3D12 backend
    d3d12 dxgi dxguid  # core stuff
    dxcompiler # for HLSL shader compilation
    DirectX-Headers # utility stuff, e.g. CD3DX12
    spirv-cross-hlsl # for spirv->hlsl
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
endif()

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
