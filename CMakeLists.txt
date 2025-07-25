cmake_minimum_required(VERSION 3.7 FATAL_ERROR)

# "MSVC runtime library flags are selected by an abstraction."
# See https://cmake.org/cmake/help/latest/policy/CMP0091.html
cmake_policy(SET CMP0091 NEW)

project(Arkose)

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})

# Set default c++ version
set(CMAKE_CXX_STANDARD 20)

# Set configuration types
set(CMAKE_CONFIGURATION_TYPES "Debug;Develop;Release")

# Set default compiler & linker flags
if (MSVC)
    set(ARKOSE_CXX_FLAGS "/Zc:preprocessor")

    # Disable warnings from external dependencies' headers which are included into our source file
    # TODO: See if CMake can use the `/external:I /external:W0` and similar for GCC/Clang to avoid this!
    set(ARKOSE_CXX_FLAGS "${ARKOSE_CXX_FLAGS} /wd4100") # 'identifier' : unreferenced formal parameter (we break this a lot ourselves - consider tidying this up)
    set(ARKOSE_CXX_FLAGS "${ARKOSE_CXX_FLAGS} /wd5054") # operator 'operator-name': deprecated between enumerations of different types (cereal break this a lot)

    set(CMAKE_CXX_FLAGS_DEBUG   "${ARKOSE_CXX_FLAGS} /Od /Ob0 /Zi /RTC1           /DARKOSE_DEBUG  ")
    set(CMAKE_CXX_FLAGS_DEVELOP "${ARKOSE_CXX_FLAGS} /O2 /Ob0 /Zi        /DNDEBUG /DARKOSE_DEVELOP")
    set(CMAKE_CXX_FLAGS_RELEASE "${ARKOSE_CXX_FLAGS} /O2 /Ob2            /DNDEBUG /DARKOSE_RELEASE") # todo: re-add /GL

    set(CMAKE_EXE_LINKER_FLAGS_DEBUG   "/DEBUG /INCREMENTAL    ")
    set(CMAKE_EXE_LINKER_FLAGS_DEVELOP "/DEBUG /INCREMENTAL    ")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "       /INCREMENTAL:NO ") # todo: add /LTCG together with /GL above
else ()
    set(ARKOSE_CXX_FLAGS "${ARKOSE_CXX_FLAGS} -Wno-unused-variable -Wno-unused-parameter")

    set(CMAKE_CXX_FLAGS_DEBUG   "${ARKOSE_CXX_FLAGS} -O0 -g                       -DARKOSE_DEBUG  ")
    set(CMAKE_CXX_FLAGS_DEVELOP "${ARKOSE_CXX_FLAGS} -O2 -g              -DNDEBUG -DARKOSE_DEVELOP")
    set(CMAKE_CXX_FLAGS_RELEASE "${ARKOSE_CXX_FLAGS} -O2                 -DNDEBUG -DARKOSE_RELEASE")

    set(CMAKE_EXE_LINKER_FLAGS_DEBUG   ${CMAKE_EXE_LINKER_FLAGS_DEBUG})
    set(CMAKE_EXE_LINKER_FLAGS_DEVELOP ${CMAKE_EXE_LINKER_FLAGS_RELEASE})
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE ${CMAKE_EXE_LINKER_FLAGS_RELEASE})
endif ()

if (MSVC)
    # map these MSVC specific configurations to develop
    set(CMAKE_CXX_FLAGS_MINSIZEREL            ${CMAKE_CXX_FLAGS_DEVELOP})
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO        ${CMAKE_CXX_FLAGS_DEVELOP})
    set(CMAKE_EXE_LINKER_FLAGS_MINSIZEREL     ${CMAKE_EXE_LINKER_FLAGS_DEVELOP})
    set(CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO ${CMAKE_EXE_LINKER_FLAGS_DEVELOP})

    # Explicitly set _ITERATOR_DEBUG_LEVEL (https://learn.microsoft.com/en-us/cpp/standard-library/iterator-debug-level)
    add_compile_definitions($<$<CONFIG:Debug>:_ITERATOR_DEBUG_LEVEL=2>)
    add_compile_definitions($<$<CONFIG:Develop>:_ITERATOR_DEBUG_LEVEL=0>)
    add_compile_definitions($<$<CONFIG:Release>:_ITERATOR_DEBUG_LEVEL=0>)
endif ()

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
elseif (UNIX)
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
    find_package(Vulkan REQUIRED COMPONENTS shaderc_combined)
endif()

##################################
# In-tree built dependencies

add_subdirectory(deps)
add_subdirectory(deps/arklib)

##################################
########## ARKOSE CORE ###########
##################################

add_subdirectory(arkcore)
add_library(ArkoseCore STATIC ${ARKCORE_SOURCES})
target_include_directories(ArkoseCore PUBLIC arkcore/)

if(MSVC)
  target_compile_options(ArkoseCore PRIVATE /W4)
else()
  # todo!
endif()
set_property(TARGET ArkoseCore PROPERTY COMPILE_WARNING_AS_ERROR ON)

if(WITH_MIMALLOC)
    target_link_libraries(ArkoseCore PUBLIC mimalloc)
    target_compile_definitions(ArkoseCore PUBLIC WITH_MIMALLOC)
endif()

# todo: see if we can move some of the libraries to PRIVATE
target_link_libraries(ArkoseCore PUBLIC
    arklib # core arkose library (math etc.)

    magic_enum # enum reflection
    fmt # string formatting (TODO: replace with stl fmt in C++20)
    half # 16-bit float
    TracyClient # real-time profiling
    concurrentqueue # lock-free concurrent queue
    nlohmann_json::nlohmann_json # json reading & writing
    offsetAllocator # good general purpose offset/slot allocator

    glfw # window and event management
    nfd # native file dialogs

    dear_imgui # dear imgui - immediate mode gui
    imguizmo # gizmo extension for dear imgui
    implot # plot extension for dear imgui

    # Geometry processing related
    eigen # linear algebra library (needed for for libigl)
    igl::core # libigl - geometry processing library
    igl_restricted::triangle # libigl triangulation

    # Asset related
    stb_image # image loading
    tiny_gltf # glTF 2.0 loading
    cereal # serialisation
    lz4_static # generic lossless compression
    bc7enc_rdo # texture compression
    mikktspace # tangent space generation
    meshoptimizer # mesh optimization & meshlet generation
    omm_sdk # opacity micro-map SDK
    )

if (MSVC)
    # Ensure we're using the dynamically linked runtime library for MSVC.
    # When linking with Tracy this is required, as it will dynamically load dlls.
    set(MSVC_RUNTIME_LIBRARY_STRING "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    set_property(TARGET ArkoseCore     PROPERTY MSVC_RUNTIME_LIBRARY ${MSVC_RUNTIME_LIBRARY_STRING})
    set_property(TARGET Jolt           PROPERTY MSVC_RUNTIME_LIBRARY ${MSVC_RUNTIME_LIBRARY_STRING})

    # Avoid including too much noise from windows.h
    target_compile_definitions(ArkoseCore PUBLIC
        WIN32_LEAN_AND_MEAN NOMINMAX
        _CRT_SECURE_NO_WARNINGS)
endif()

set(ARKOSE_SYSTEM_GLFW False) # default off

# Set up platform-specific defines
if (WIN32)
    target_compile_definitions(ArkoseCore PUBLIC PLATFORM_WINDOWS=1)
    set(ARKOSE_SYSTEM_GLFW True)
elseif (APPLE)
    # TODO: Make sure it's not iOS or similar
    target_compile_definitions(ArkoseCore PUBLIC PLATFORM_MACOS=1)
    set(ARKOSE_SYSTEM_GLFW True)
elseif (UNIX)
    target_compile_definitions(ArkoseCore PUBLIC PLATFORM_LINUX=1)
    set(ARKOSE_SYSTEM_GLFW True)
endif ()

# TODO: Disable tracy for release!
# Keep tracy enabled at all time but with on-demand only, meaning we don't accumulate tracy data
# in the Arkose process which can hog a lot of memory, which looks a lot like a memory leak.
target_compile_definitions(ArkoseCore PUBLIC TRACY_ENABLE)
# Disable on-demand for now as the latest version seems to not work correctly with it
#target_compile_definitions(ArkoseCore PUBLIC TRACY_ON_DEMAND)

##################################
######## ARKOSE RENDERER #########
##################################

add_subdirectory(arkose)
add_executable(ArkoseRenderer ${ARKOSE_SOURCES})
target_include_directories(ArkoseRenderer PRIVATE arkose/)

if(MSVC)
  target_compile_options(ArkoseRenderer PRIVATE /W4)
else()
  # todo!
endif()
set_property(TARGET ArkoseRenderer PROPERTY COMPILE_WARNING_AS_ERROR ON)

if (ARKOSE_BACKEND_VULKAN)
    message(STATUS "ArkoseRenderer: compiling Vulkan graphics backend")
    target_compile_definitions(ArkoseCore PUBLIC WITH_VULKAN)
    target_sources(ArkoseRenderer PRIVATE
        arkose/rendering/backend/vulkan/VulkanBackend.cpp
        arkose/rendering/backend/vulkan/VulkanBackend.h
        arkose/rendering/backend/vulkan/VulkanBindingSet.cpp
        arkose/rendering/backend/vulkan/VulkanBindingSet.h
        arkose/rendering/backend/vulkan/VulkanBuffer.cpp
        arkose/rendering/backend/vulkan/VulkanBuffer.h
        arkose/rendering/backend/vulkan/VulkanCommandList.cpp
        arkose/rendering/backend/vulkan/VulkanCommandList.h
        arkose/rendering/backend/vulkan/VulkanComputeState.cpp
        arkose/rendering/backend/vulkan/VulkanComputeState.h
        arkose/rendering/backend/vulkan/VulkanRenderState.cpp
        arkose/rendering/backend/vulkan/VulkanRenderState.h
        arkose/rendering/backend/vulkan/VulkanRenderTarget.cpp
        arkose/rendering/backend/vulkan/VulkanRenderTarget.h
        arkose/rendering/backend/vulkan/VulkanSampler.cpp
        arkose/rendering/backend/vulkan/VulkanSampler.h
        arkose/rendering/backend/vulkan/VulkanTexture.cpp
        arkose/rendering/backend/vulkan/VulkanTexture.h
        arkose/rendering/backend/vulkan/VulkanUpscalingState.cpp
        arkose/rendering/backend/vulkan/VulkanUpscalingState.h
        arkose/rendering/backend/vulkan/extensions/debug-utils/VulkanDebugUtils.cpp
        arkose/rendering/backend/vulkan/extensions/debug-utils/VulkanDebugUtils.h
        arkose/rendering/backend/vulkan/extensions/mesh-shader-ext/VulkanMeshShaderEXT.cpp
        arkose/rendering/backend/vulkan/extensions/opacity-micromap-ext/VulkanOpacityMicromapEXT.cpp
        arkose/rendering/backend/vulkan/extensions/opacity-micromap-ext/VulkanOpacityMicromapEXT.h
        arkose/rendering/backend/vulkan/extensions/mesh-shader-ext/VulkanMeshShaderEXT.h
        arkose/rendering/backend/vulkan/extensions/ray-tracing-khr/VulkanRayTracingKHR.cpp
        arkose/rendering/backend/vulkan/extensions/ray-tracing-khr/VulkanRayTracingKHR.h
        arkose/rendering/backend/vulkan/extensions/ray-tracing-khr/VulkanAccelerationStructureKHR.cpp
        arkose/rendering/backend/vulkan/extensions/ray-tracing-khr/VulkanAccelerationStructureKHR.h
        arkose/rendering/backend/vulkan/extensions/ray-tracing-khr/VulkanRayTracingStateKHR.cpp
        arkose/rendering/backend/vulkan/extensions/ray-tracing-khr/VulkanRayTracingStateKHR.h
        )

    add_subdirectory(deps/vulkan)

    target_link_libraries(ArkoseRenderer PRIVATE
        Vulkan::Vulkan
        VulkanMemoryAllocator_static
        Vulkan::shaderc_combined # glsl shader compiler
        spirv-cross-core # spir-v reflection
        )
endif()

if (ARKOSE_BACKEND_D3D12)
    message(STATUS "ArkoseRenderer: compiling D3D12 graphics backend")
    target_compile_definitions(ArkoseCore PUBLIC WITH_D3D12)
    target_sources(ArkoseRenderer PRIVATE
        arkose/rendering/backend/d3d12/D3D12Backend.cpp
        arkose/rendering/backend/d3d12/D3D12Backend.h
        arkose/rendering/backend/d3d12/D3D12BindingSet.cpp
        arkose/rendering/backend/d3d12/D3D12BindingSet.h
        arkose/rendering/backend/d3d12/D3D12Buffer.cpp
        arkose/rendering/backend/d3d12/D3D12Buffer.h
        arkose/rendering/backend/d3d12/D3D12CommandList.cpp
        arkose/rendering/backend/d3d12/D3D12CommandList.h
        arkose/rendering/backend/d3d12/D3D12ComputeState.cpp
        arkose/rendering/backend/d3d12/D3D12ComputeState.h
        arkose/rendering/backend/d3d12/D3D12DescriptorHeapAllocator.cpp
        arkose/rendering/backend/d3d12/D3D12DescriptorHeapAllocator.h
        arkose/rendering/backend/d3d12/D3D12RenderState.cpp
        arkose/rendering/backend/d3d12/D3D12RenderState.h
        arkose/rendering/backend/d3d12/D3D12RenderTarget.cpp
        arkose/rendering/backend/d3d12/D3D12RenderTarget.h
        arkose/rendering/backend/d3d12/D3D12Sampler.cpp
        arkose/rendering/backend/d3d12/D3D12Sampler.h
        arkose/rendering/backend/d3d12/D3D12Texture.cpp
        arkose/rendering/backend/d3d12/D3D12Texture.h
        arkose/rendering/backend/shader/dxc/DxcInterface.cpp
        arkose/rendering/backend/shader/dxc/DxcInterface.h
        )

    add_subdirectory(deps/d3d12)

    target_link_libraries(ArkoseRenderer PRIVATE
        agilitysdk # for latest D3D12 headers & DLLs
        agilitysdk_d3dx12 # d3dx12 helper library from the agility sdk
        d3d12 # core library, assumed to be agility sdk compatible
        dxgi # surface setup etc.
        dxguid # GUIDs for relevant directx types
        dxc # for HLSL shader compilation
        spirv-cross-hlsl # for spirv->hlsl transpiling
        D3D12MemoryAllocator # vram memory allocation
        )

    # Copy Agility SDK DLLs to correct location & pass version to the target
    # See https://devblogs.microsoft.com/directx/gettingstarted-dx12agility/ for more info
    target_compile_definitions(ArkoseRenderer PRIVATE -DARKOSE_AGILITY_SDK_VERSION=${AGILITYSDK_VERSION_MAJOR})
    add_custom_command(TARGET ArkoseRenderer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:ArkoseRenderer>/D3D12/"
        COMMAND ${CMAKE_COMMAND} -E copy        -t "$<TARGET_FILE_DIR:ArkoseRenderer>/D3D12/" ${AGILITYSDK_D3D12CORE_DLL}
        COMMAND ${CMAKE_COMMAND} -E copy        -t "$<TARGET_FILE_DIR:ArkoseRenderer>/D3D12/" ${AGILITYSDK_D3D12SDKLAYERS_DLL}
        COMMAND_EXPAND_LISTS
        )
endif()

if (ARKOSE_SYSTEM_GLFW)
    target_compile_definitions(ArkoseCore PUBLIC WITH_GLFW)
    target_compile_definitions(ArkoseCore PUBLIC ARKOSE_USE_DEFAULT_MAIN)
    target_sources(ArkoseRenderer PRIVATE
        arkose/system/glfw/SystemGlfw.cpp
        )
else()
    message(SEND_ERROR "No system specified!")
endif()

if(MSVC)
    target_sources(ArkoseRenderer PUBLIC "arkose/Arkose.natvis")
endif()

if (MSVC)
    # Ensure we're using the dynamically linked runtime library for MSVC.
    # When linking with Tracy this is required, as it will dynamically load dlls.
    set(MSVC_RUNTIME_LIBRARY_STRING "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    set_property(TARGET ArkoseRenderer PROPERTY MSVC_RUNTIME_LIBRARY ${MSVC_RUNTIME_LIBRARY_STRING})
endif()

target_link_libraries(ArkoseRenderer PRIVATE
    ArkoseCore # core arkose library

    # Debug tools
    renderdoc_api

    # Physics
    Jolt
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

if(MSVC)
    # the debug version of shaderc doesn't ship with .pdb files in the VulkanSDK, so let's supress the warning about missing pdb files
    target_link_options(ArkoseRenderer PRIVATE "/ignore:4099")
endif()

##################################
#### CUSTOM BUILD STEPS ETC. #####
##################################

# Make assets & shaders accessible in runtime relative to the exe-location

add_custom_command(TARGET ArkoseRenderer PRE_BUILD
    COMMAND ${CMAKE_COMMAND} -E create_symlink "${CMAKE_SOURCE_DIR}/assets/imported" "$<TARGET_FILE_DIR:ArkoseRenderer>/assets"
    COMMAND ${CMAKE_COMMAND} -E create_symlink "${CMAKE_SOURCE_DIR}/arkose/shaders" "$<TARGET_FILE_DIR:ArkoseRenderer>/shaders"
    COMMENT "Creating symlinks to the assets and shaders so they will be locatable relative to the executable"
    VERBATIM)

##################################
### PRETTIFY THE PROJECT FILES ###
##################################

get_target_property(ARKCORE_SOURCES ArkoseCore SOURCES)
get_target_property(ARKOSE_SOURCES ArkoseRenderer SOURCES)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/arkcore" FILES ${ARKCORE_SOURCES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/arkose" FILES ${ARKOSE_SOURCES})

function(iterate_all_targets dir)
    get_property(targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(target IN LISTS targets)
        if(NOT target STREQUAL "ArkoseCore" AND NOT target STREQUAL "ArkoseRenderer")
            set_target_properties(${target} PROPERTIES FOLDER "deps")
        endif()
    endforeach()

    get_property(subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
    foreach(subdir IN LISTS subdirs)
        iterate_all_targets("${subdir}")
    endforeach()
endfunction()

iterate_all_targets(.)

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT "ArkoseRenderer")
set_target_properties(ArkoseRenderer PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:ArkoseRenderer>")

# Map both Develop & Release configurations to MinSizeRel and/or RelWithDebInfo
# Not sure if this is needed..?
set_target_properties(ArkoseRenderer PROPERTIES
    MAP_IMPORTED_CONFIG_MINSIZEREL     Develop
    MAP_IMPORTED_CONFIG_RELWITHDEBINFO Develop
    MAP_IMPORTED_CONFIG_MINSIZEREL     Release
    MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release
    )

##################################
######### ARKOSE TOOLING #########
##################################

add_subdirectory(tools)

##################################
