
##################################
###### Command line tools ########
##################################

# .. todo!

##################################
## OpenUSD-specific tooling WIP ##
##################################

if((DEFINED {PXR_USD_LOCATION}) OR (DEFINED ENV{PXR_USD_LOCATION}))
    message(STATUS "Will attempt to build USD tools")
    add_subdirectory(usd)
else()
    message(STATUS "Will NOT attempt to build USD tools as PXR_USD_LOCATION is not set")
endif()

##################################
