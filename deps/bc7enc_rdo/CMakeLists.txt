#
# bc7enc_rdo
#

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

    add_library(bc7enc_rdo
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
    target_include_directories(bc7enc_rdo INTERFACE ${bc7enc_rdo_fetch_SOURCE_DIR})

    find_package(OpenMP)
    if(OPENMP_FOUND)
        message(STATUS "bc7enc_rdo: found OpenMP, enabling OpenMP parallelization in bc7enc_rdo")
        target_link_libraries(bc7enc_rdo PRIVATE OpenMP::OpenMP_CXX)
    else()
        message(STATUS "bc7enc_rdo: OpenMP not found, will not use OpenMP parallelization in bc7enc_rdo")
    endif()

endif()
