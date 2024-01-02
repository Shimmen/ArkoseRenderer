#include "PhysicsBackend.h"

#include "core/Logging.h"
#include "utility/CommandLine.h"
#include "utility/Profiling.h"
#include "physics/backend/jolt/JoltPhysicsBackend.h"

PhysicsBackend* PhysicsBackend::s_globalPhysicsBackend { nullptr };

PhysicsBackend* PhysicsBackend::create()
{
    SCOPED_PROFILE_ZONE();

    ARKOSE_ASSERT(s_globalPhysicsBackend == nullptr);

    if (CommandLine::hasArgument("-nophysics")) {
        ARKOSE_LOG(Info, "PhysicsBackend: none (due to '-nophysics')");
        return nullptr;
    }

    s_globalPhysicsBackend = new JoltPhysicsBackend();

    if (s_globalPhysicsBackend->initialize()) {
        ARKOSE_LOG(Info, "PhysicsBackend: Jolt physics backend initialized");
    } else {
        ARKOSE_LOG(Fatal, "PhysicsBackend: could not initialize physics backend, exiting.");
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
