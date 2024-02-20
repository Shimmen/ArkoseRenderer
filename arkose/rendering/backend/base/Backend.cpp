#include "Backend.h"

#include "core/CommandLine.h"

#if WITH_VULKAN
#include "rendering/backend/vulkan/VulkanBackend.h"
#endif

#if WITH_D3D12
#include "rendering/backend/d3d12/D3D12Backend.h"
#endif

Backend* Backend::s_globalBackend { nullptr };

Backend& Backend::create(Backend::AppSpecification const& appSpecification)
{
    SCOPED_PROFILE_ZONE();

    // Prefer vulkan if it's available
    // TODO: How do we want to handle other platforms here? Maybe leave this backend creation to the system?
    Backend::Type backendType = Backend::Type::Vulkan;

#if WITH_VULKAN
    if (CommandLine::hasArgument("-vulkan")) {
        backendType = Backend::Type::Vulkan;
    }
#endif

#if WITH_D3D12
    if (CommandLine::hasArgument("-d3d12")) {
        backendType = Backend::Type::D3D12;
    }
#endif

    switch (backendType) {
    case Backend::Type::Vulkan:
        #if WITH_VULKAN
        s_globalBackend = new VulkanBackend({}, appSpecification);
        #else
        ARKOSE_LOG_FATAL("Trying to create Vulkan backend which is not included in this build, exiting.");
        #endif
        break;
    case Backend::Type::D3D12:
        #if WITH_D3D12
        s_globalBackend = new D3D12Backend({}, appSpecification);
        #else
        ARKOSE_LOG_FATAL("Trying to create D3D12 backend which is not included in this build, exiting.");
        #endif
        break;
    }

    return *s_globalBackend;
}

void Backend::destroy()
{
    ARKOSE_ASSERT(s_globalBackend != nullptr);
    delete s_globalBackend;
    s_globalBackend = nullptr;
}

Backend& Backend::get()
{
    ARKOSE_ASSERT(s_globalBackend != nullptr);
    return *s_globalBackend;
}

std::string Backend::capabilityName(Capability capability)
{
    switch (capability) {
    case Capability::RayTracing:
        return "RayTracing";
    case Capability::Shader16BitFloat:
        return "Shader16BitFloat";
    default:
        ASSERT_NOT_REACHED();
    }
}
