
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
    GIT_TAG        "v0.0.1")

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

##################################
# Custom built dependencies that either:
#  1. can't becompiled as-is, e.g. needs additional files (header-only libs needing a cpp for making a static lib)
#  2. can't easily be downloaded using FetchContent or need some files to be copied around
#  3. do not have a CMakeLists.txt file, or need a custom one to work nicely with Arkose

add_subdirectory(aftermath_sdk)
add_subdirectory(stb_image)
add_subdirectory(tiny_gltf)
add_subdirectory(half)
add_subdirectory(dlss)
add_subdirectory(mimalloc)
add_subdirectory(nrd)
add_subdirectory(omm_sdk)
add_subdirectory(renderdoc)
add_subdirectory(slang)
add_subdirectory(tomlplusplus)
add_subdirectory(imgui)
add_subdirectory(mikktspace)
add_subdirectory(eigen)
add_subdirectory(bc7enc_rdo)

##################################
# Final dependency build setup

# For libigl, specifically include the triangle module
igl_include(restricted triangle)
