#include "Backend.h"

std::string Backend::capabilityName(Capability capability)
{
    switch (capability) {
    case Capability::RtxRayTracing:
        return "RtxRayTracing";
    case Capability::Shader16BitFloat:
        return "Shader16BitFloat";
    case Capability::ShaderTextureArrayDynamicIndexing:
        return "ShaderTextureArrayDynamicIndexing";
    case Capability::ShaderBufferArrayDynamicIndexing:
        return "ShaderBufferArrayDynamicIndexing";
    default:
        ASSERT_NOT_REACHED();
    }
}
