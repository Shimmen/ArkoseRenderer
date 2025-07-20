#
# ImGui and related dependencies
#

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
