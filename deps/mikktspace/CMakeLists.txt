#
# MikkTSpace
#

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
