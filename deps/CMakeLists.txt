
include(FetchContent)

set_directory_properties(PROPERTIES EXCLUDE_FROM_ALL YES)

##################################
# FetchContent dependencies with CMakeLists.txt

FetchContent_Declare(magic_enum
    GIT_REPOSITORY https://github.com/Neargye/magic_enum.git
    GIT_TAG        "v0.9.7")

FetchContent_Declare(json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        "v3.11.3")

set(BUILD_DOC OFF)
set(BUILD_SANDBOX OFF)
set(SKIP_PERFORMANCE_COMPARISON ON)
FetchContent_Declare(cereal
    GIT_REPOSITORY https://github.com/USCiLab/cereal.git
    GIT_TAG        "v1.3.2")

FetchContent_Declare(fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG        "11.1.4")

set(GLFW_BUILD_EXAMPLES OFF)
set(GLFW_BUILD_TESTS OFF)
FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        "3.4")

FetchContent_Declare(nativefiledialog-extended
    GIT_REPOSITORY https://github.com/btzy/nativefiledialog-extended.git
    GIT_TAG        "v1.2.1")

FetchContent_Declare(meshoptimizer
    GIT_REPOSITORY https://github.com/zeux/meshoptimizer.git
    GIT_TAG        "v0.23")

set(TARGET_UNIT_TESTS OFF)
set(TARGET_HELLO_WORLD OFF)
set(TARGET_PERFORMANCE_TEST OFF)
set(TARGET_SAMPLES OFF)
set(TARGET_VIEWER OFF)
FetchContent_Declare(jolt-physics
    GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
    GIT_TAG        "v5.1.0"
    SOURCE_SUBDIR  "Build")

set(LZ4_BUILD_CLI OFF)
set(LZ4_BUILD_LEGACY_LZ4C OFF)
FetchContent_Declare(lz4
    GIT_REPOSITORY https://github.com/lz4/lz4.git
    GIT_TAG        "v1.9.4"
    SOURCE_SUBDIR  "build/cmake")

FetchContent_Declare(concurrentqueue
    GIT_REPOSITORY https://github.com/cameron314/concurrentqueue.git
    GIT_TAG        "v1.0.4")

FetchContent_Declare(tracy
    GIT_REPOSITORY https://github.com/wolfpld/tracy.git
    GIT_TAG        "v0.11.1"
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE)

FetchContent_Declare(libigl
    GIT_REPOSITORY https://github.com/libigl/libigl.git
    GIT_TAG        "v2.5.0")

FetchContent_Declare(offset-allocator
    GIT_REPOSITORY https://github.com/Shimmen/OffsetAllocator.git
    GIT_TAG        "f67c05012310c1577205d5795a9302f1daebddf0") # (latest commit at the time of writing)

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
        "${dearimgui_fetch_SOURCE_DIR}/backends/imgui_impl_glfw.cpp")

    if(ARKOSE_BACKEND_VULKAN)
        target_sources(dear_imgui PRIVATE "${dearimgui_fetch_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp")
        target_link_libraries(dear_imgui PRIVATE Vulkan::Vulkan)
    endif()
    if(ARKOSE_BACKEND_D3D12)
        target_sources(dear_imgui PRIVATE "${dearimgui_fetch_SOURCE_DIR}/backends/imgui_impl_dx12.cpp")
        target_link_libraries(dear_imgui PRIVATE d3d12 dxgi)
    endif()

    target_compile_features(dear_imgui PRIVATE cxx_std_17)
    target_include_directories(dear_imgui PUBLIC ${dearimgui_fetch_SOURCE_DIR})

    target_link_libraries(dear_imgui PRIVATE glfw)

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

FetchContent_Declare(implot_fetch
    GIT_REPOSITORY https://github.com/epezent/implot.git
    GIT_TAG        "v0.16")
FetchContent_GetProperties(implot_fetch)
if(NOT implot_fetch_POPULATED)
    FetchContent_Populate(implot_fetch)

    add_library(implot
        "${implot_fetch_SOURCE_DIR}/implot.cpp"
        "${implot_fetch_SOURCE_DIR}/implot_demo.cpp"
        "${implot_fetch_SOURCE_DIR}/implot_items.cpp"
        )

    target_compile_features(implot PRIVATE cxx_std_20)
    target_include_directories(implot PUBLIC ${implot_fetch_SOURCE_DIR})
    target_link_libraries(implot PRIVATE dear_imgui)

endif()

# Temporarily(TM) switch out the repo to a fork which adds a missing include of cstdint. Hopefully it will be upstreamed at some point.
FetchContent_Declare(bc7enc_rdo_fetch
    GIT_REPOSITORY https://github.com/Sewer56/bc7enc_rdo
    GIT_TAG        ba0b7de9843e467ef804557ac228fe7d2a39cdb0)
    #GIT_REPOSITORY https://github.com/richgel999/bc7enc_rdo.git
    #GIT_TAG        "e6990bc11829c072d9f9e37296f3335072aab4e4")
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

    find_package(OpenMP)
    if(OPENMP_FOUND)
        target_link_libraries(bc7enc_lib PRIVATE OpenMP::OpenMP_CXX)
    endif()

endif()

FetchContent_Declare(mikktspace_fetch
    GIT_REPOSITORY https://github.com/mmikk/MikkTSpace.git
    GIT_TAG        3e895b49d05ea07e4c2133156cfa94369e19e409)
FetchContent_GetProperties(mikktspace_fetch)
if(NOT mikktspace_fetch_POPULATED)
    FetchContent_Populate(mikktspace_fetch)

    add_library(mikktspace
        "${mikktspace_fetch_SOURCE_DIR}/mikktspace.c"
        "${mikktspace_fetch_SOURCE_DIR}/mikktspace.h"
        )
    target_include_directories(mikktspace PUBLIC ${mikktspace_fetch_SOURCE_DIR})

endif()

# Eigen does come with a CMakeLists.txt but it's not intended for library use
FetchContent_Declare(eigen_fetch
    GIT_REPOSITORY https://gitlab.com/libeigen/eigen.git
    GIT_TAG        "3.4.0")
FetchContent_GetProperties(eigen_fetch)
if(NOT eigen_fetch_POPULATED)
    FetchContent_Populate(eigen_fetch)

    add_library(eigen INTERFACE)
    target_include_directories(eigen INTERFACE ${eigen_fetch_SOURCE_DIR})
endif()

##################################
# Custom built dependencies that either:
#  1. can't becompiled as-is, e.g. needs additional files (header-only libs needing a cpp for making a static lib)
#  2. can't easily be downloaded using FetchContent or need some files to be copied around

add_subdirectory(stb_image)
add_subdirectory(tiny_gltf)
add_subdirectory(half)
add_subdirectory(dlss)
add_subdirectory(mimalloc)
add_subdirectory(omm_sdk)
add_subdirectory(renderdoc)

##################################
# Finalize FetchContent dependencies

FetchContent_MakeAvailable(
    magic_enum
    json
    cereal
    fmt
    glfw
    nativefiledialog-extended
    meshoptimizer
    jolt-physics
    lz4
    concurrentqueue
    tracy
    libigl
    offset-allocator)

# Final dependency build setup
igl_include(restricted triangle)
