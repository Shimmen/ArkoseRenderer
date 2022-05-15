
include(FetchContent)

set_directory_properties(PROPERTIES EXCLUDE_FROM_ALL YES)

##################################
# FetchContent dependencies with CMakeLists.txt

set(MOOSLIB_BuildTests OFF)
FetchContent_Declare(mooslib
    GIT_REPOSITORY https://github.com/Shimmen/mooslib.git
    GIT_TAG        46d4420381ee38e96ffe1aef9936168719a4f3f6)

FetchContent_Declare(json
    GIT_REPOSITORY https://github.com/ArthurSonzogni/nlohmann_json_cmake_fetchcontent.git
    GIT_TAG        v3.7.3)

FetchContent_Declare(fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG        8.1.1)

set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        "3.3-stable")

FetchContent_Declare(vulkan_memory_allocator
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG        "v3.0.0")

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
    GIT_TAG        "v0.8")
FetchContent_GetProperties(tracy_fetch)
if(NOT tracy_fetch_POPULATED)
    FetchContent_Populate(tracy_fetch)

    add_library(tracy INTERFACE)
    target_include_directories(tracy INTERFACE ${tracy_fetch_SOURCE_DIR})

    # TracyClient.cpp must be compiled as part of the main executable (can't be linked in) so we have to export its path
    set(TRACY_CLIENT_SOURCE "${tracy_fetch_SOURCE_DIR}/TracyClient.cpp" PARENT_SCOPE)

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

FetchContent_MakeAvailable(mooslib json fmt glfw vulkan_memory_allocator)
