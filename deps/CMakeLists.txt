
include(FetchContent)

set_directory_properties(PROPERTIES EXCLUDE_FROM_ALL YES)

##################################
# FetchContent dependencies with CMakeLists.txt

set(SPIRV_CROSS_SKIP_INSTALL ON CACHE INTERNAL "")
set(SPIRV_CROSS_ENABLE_TESTS OFF CACHE INTERNAL "")
set(SPIRV_CROSS_CLI OFF CACHE INTERNAL "")
FetchContent_Declare(spirv-cross
    GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Cross.git
    GIT_TAG        "sdk-1.3.211.0")

FetchContent_Declare(json
    GIT_REPOSITORY https://github.com/ArthurSonzogni/nlohmann_json_cmake_fetchcontent.git
    GIT_TAG        "v3.7.3")

FetchContent_Declare(fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG        "8.1.1")

set(GLFW_BUILD_EXAMPLES OFF)
set(GLFW_BUILD_TESTS OFF)
FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        "3.3-stable")

FetchContent_Declare(nativefiledialog-extended
    GIT_REPOSITORY https://github.com/btzy/nativefiledialog-extended.git
    GIT_TAG        "33115928184cc3cc1b945a0f94bbc574d137224c")

FetchContent_Declare(vulkan_memory_allocator
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG        "v3.0.0")

FetchContent_Declare(directx_headers
    GIT_REPOSITORY https://github.com/microsoft/DirectX-Headers.git
    GIT_TAG        "v1.602.0")

set(TARGET_UNIT_TESTS OFF)
set(TARGET_HELLO_WORLD OFF)
set(TARGET_PERFORMANCE_TEST OFF)
set(TARGET_SAMPLES OFF)
set(TARGET_VIEWER OFF)
FetchContent_Declare(jolt-physics
    GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
    GIT_TAG        "84d4c6f01ef2aac7c11695366b5007cbbda1477e" # (latest release doesn't include https://github.com/jrouwe/JoltPhysics/pull/175 which we need)
    SOURCE_SUBDIR  "Build")

##################################
# FetchContent dependencies without cmake files

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

FetchContent_Declare(tracy_fetch
    GIT_REPOSITORY https://github.com/wolfpld/tracy.git
    GIT_TAG        "v0.8.2")
FetchContent_GetProperties(tracy_fetch)
if(NOT tracy_fetch_POPULATED)
    FetchContent_Populate(tracy_fetch)

    add_library(tracy INTERFACE)
    target_include_directories(tracy INTERFACE ${tracy_fetch_SOURCE_DIR})

    # TracyClient.cpp must be compiled as part of the main executable (can't be linked in) so we have to export its path
    set(TRACY_CLIENT_SOURCE "${tracy_fetch_SOURCE_DIR}/TracyClient.cpp" PARENT_SCOPE)

endif()

FetchContent_Declare(bc7enc_rdo_fetch
    GIT_REPOSITORY https://github.com/richgel999/bc7enc_rdo.git
    GIT_TAG        "e6990bc11829c072d9f9e37296f3335072aab4e4")
FetchContent_GetProperties(bc7enc_rdo_fetch)
if(NOT bc7enc_rdo_fetch_POPULATED)
    FetchContent_Populate(bc7enc_rdo_fetch)

    # TODO: Optionally make use of the ISPC bc7e.ispc file in place of bc7enc.cpp

    add_library(bc7enc_lib
        "${bc7enc_rdo_fetch_SOURCE_DIR}/rgbcx.cpp"  # for BC1-5
        "${bc7enc_rdo_fetch_SOURCE_DIR}/bc7enc.cpp" # for BC7
        "${bc7enc_rdo_fetch_SOURCE_DIR}/ert.cpp"    # for RDO post-processing

        # Nice interface wrapping all the above
        "${bc7enc_rdo_fetch_SOURCE_DIR}/rdo_bc_encoder.cpp"

        # Dependencies for rdo_bc_encoder
        "${bc7enc_rdo_fetch_SOURCE_DIR}/utils.cpp"
        "${bc7enc_rdo_fetch_SOURCE_DIR}/bc7decomp.cpp"
        "${bc7enc_rdo_fetch_SOURCE_DIR}/bc7decomp_ref.cpp"
        "${bc7enc_rdo_fetch_SOURCE_DIR}/lodepng.cpp"
        )
    target_include_directories(bc7enc_lib INTERFACE ${bc7enc_rdo_fetch_SOURCE_DIR})

    find_package(OpenMP REQUIRED)
    if(OPENMP_FOUND)
        target_link_libraries(bc7enc_lib PRIVATE OpenMP::OpenMP_CXX)
    endif()

endif()

##################################
# Custom built dependencies that either:
#  1. can't becompiled as-is, e.g. needs additional files (header-only libs needing a cpp for making a static lib)
#  2. can't easily be downloaded using FetchContent or need some files to be copied around

add_subdirectory(stb_image)
add_subdirectory(tiny_gltf)
add_subdirectory(half)

##################################
# Finalize FetchContent dependencies

FetchContent_MakeAvailable(spirv-cross json fmt glfw nativefiledialog-extended vulkan_memory_allocator directx_headers jolt-physics)
