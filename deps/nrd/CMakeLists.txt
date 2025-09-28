#
# NVIDIA Real-time Denoising (NRD) library
#

# Disable for now as it's breaking the CI build (looks like some dependency is gone?)
# We're not using it yet anyways, so it's not a big loss for the time being.
set(NRD_AVAILABLE OFF CACHE INTERNAL "")

#set(NRD_STATIC_LIBRARY ON CACHE BOOL "")
#
#FetchContent_Declare(nrd
#    GIT_REPOSITORY https://github.com/NVIDIA-RTX/NRD.git
#    GIT_TAG        v4.15.1-fixed)
#FetchContent_MakeAvailable(nrd)
