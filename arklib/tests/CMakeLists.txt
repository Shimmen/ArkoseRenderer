
include(FetchContent)

FetchContent_Declare(fmt GIT_REPOSITORY https://github.com/fmtlib/fmt GIT_TAG 6.2.0)
FetchContent_GetProperties(fmt)
if (NOT fmt_POPULATED)
    FetchContent_Populate(fmt)
    add_subdirectory(${fmt_SOURCE_DIR} ${fmt_BINARY_DIR})
endif ()

add_executable(ArklibTests basic_usage.cpp)
target_link_libraries(BasicUsage PRIVATE arklib)
target_link_libraries(BasicUsage PRIVATE fmt)
