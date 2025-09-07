#
# NVIDIA Real-time Denoising (NRD) library
#

set(NRD_STATIC_LIBRARY ON CACHE BOOL "")

FetchContent_Declare(nrd
    GIT_REPOSITORY https://github.com/NVIDIA-RTX/NRD.git
    GIT_TAG        v4.15.1-fixed)
FetchContent_MakeAvailable(nrd)
