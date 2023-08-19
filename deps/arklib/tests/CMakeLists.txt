
include(FetchContent)

FetchContent_Declare(fmt GIT_REPOSITORY https://github.com/fmtlib/fmt GIT_TAG 6.2.0)
FetchContent_GetProperties(fmt)
if (NOT fmt_POPULATED)
    FetchContent_Populate(fmt)
    add_subdirectory(${fmt_SOURCE_DIR} ${fmt_BINARY_DIR})
endif ()

add_executable(ArklibTests basic_usage.cpp)
set_property(TARGET ArklibTests PROPERTY CXX_STANDARD 20)
target_link_libraries(ArklibTests PRIVATE arklib)
target_link_libraries(ArklibTests PRIVATE fmt)
