#include "Backend.h"

#include "rendering/backend/d3d12/D3D12Backend.h"
#include "rendering/backend/vulkan/VulkanBackend.h"

Backend* Backend::s_globalBackend { nullptr };

Backend& Backend::create(Backend::Type backendType, GLFWwindow* window, const Backend::AppSpecification& appSpecification)
{
    SCOPED_PROFILE_ZONE();

    switch (backendType) {
    case Backend::Type::Vulkan:
        s_globalBackend = new VulkanBackend({}, window, appSpecification);
        break;
    case Backend::Type::D3D12:
        s_globalBackend = new D3D12Backend({}, window, appSpecification);
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