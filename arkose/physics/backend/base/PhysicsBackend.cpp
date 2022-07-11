#include "PhysicsBackend.h"

#include "core/Logging.h"
#include "utility/Profiling.h"
#include "physics/backend/jolt/JoltPhysicsBackend.h"

PhysicsBackend* PhysicsBackend::s_globalPhysicsBackend { nullptr };

PhysicsBackend* PhysicsBackend::create(PhysicsBackend::Type physicsBackendType)
{
    SCOPED_PROFILE_ZONE();

    ARKOSE_ASSERT(s_globalPhysicsBackend == nullptr);

    switch (physicsBackendType) {
    case PhysicsBackend::Type::None:
        return nullptr;
    case PhysicsBackend::Type::Jolt:
        s_globalPhysicsBackend = new JoltPhysicsBackend();
        break;
    default:
        ASSERT_NOT_REACHED();
    }

    if (!s_globalPhysicsBackend->initialize()) {
        ARKOSE_LOG(Fatal, "could not initialize physics backend, exiting.");
    }

    return s_globalPhysicsBackend;
}

void PhysicsBackend::destroy()
{
    if (s_globalPhysicsBackend) {
        s_globalPhysicsBackend->shutdown();

        delete s_globalPhysicsBackend;
        s_globalPhysicsBackend = nullptr;
    }
}
