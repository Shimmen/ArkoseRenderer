#include "System.h"

#include "core/Assert.h"

#if defined(WITH_GLFW)
#include "system/glfw/SystemGlfw.h"
#endif

System* System::s_system { nullptr };

bool System::initialize()
{
#if defined(WITH_GLFW)
    s_system = new SystemGlfw();
    return true;
#else
    #error "No system specified!"
    return false;
#endif
}

void System::shutdown()
{
    delete s_system;
    s_system = nullptr;
}

System& System::get()
{
    ARKOSE_ASSERT(s_system != nullptr);
    return *s_system;
}
