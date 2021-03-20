#include "Backend.h"

std::string Backend::capabilityName(Capability capability)
{
    switch (capability) {
    case Capability::RtxRayTracing:
        return "RtxRayTracing";
    case Capability::Shader16BitFloat:
        return "Shader16BitFloat";
    default:
        ASSERT_NOT_REACHED();
    }
}
