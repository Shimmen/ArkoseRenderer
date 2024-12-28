cmake_minimum_required(VERSION 3.7)
project(mimalloc)

if(False)#(MSVC)
    set(WITH_MIMALLOC True CACHE INTERNAL "")
    message(STATUS "Will use mimalloc for all allocations (with mimalloc override mode)")

    add_library(mimalloc SHARED IMPORTED GLOBAL)
    target_include_directories(mimalloc INTERFACE "include/")

    set_property(TARGET mimalloc PROPERTY IMPORTED_IMPLIB   "${CMAKE_CURRENT_LIST_DIR}/bin/msvc_vs2022/mimalloc-override.lib")
    set_property(TARGET mimalloc PROPERTY IMPORTED_LOCATION "${CMAKE_CURRENT_LIST_DIR}/bin/msvc_vs2022/mimalloc-override.dll"
                                                            "${CMAKE_CURRENT_LIST_DIR}/bin/msvc_vs2022/mimalloc-redirect.dll")
else()
    set(WITH_MIMALLOC False CACHE INTERNAL "")
    message(STATUS "No platform setup for mimalloc - default malloc will be used for this platform")
endif()
