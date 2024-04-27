#include "MemoryManager.h"

#include "core/Logging.h"
#include <ark/compiler.h>

#if WITH_MIMALLOC
#include <mimalloc.h>
#if _WIN32
// From mimalloc documentation:
//  "For best performance on Windows with C++, it is also recommended to also override the new/delete
//   operations (by including mimalloc-new-delete.h in a single(!) source file in your project)."
#include <mimalloc-new-delete.h>
#endif
#endif // WITH_MIMALLOC

// Disable optimizations to ensure we don't optimize away any callls to mimalloc,
// which may cause the the mimalloc override libraries to be loaded later or not at all.
ARK_DISABLE_OPTIMIZATIONS
void MemoryManager::initialize()
{
#if WITH_MIMALLOC
    // Calling this ensures that the DLLs are loaded
    int mimallocVersion = mi_version();
    (void)mimallocVersion;

    mi_option_set(mi_option_show_errors, 1);
#if defined(ARKOSE_DEBUG)
    mi_option_set(mi_option_show_stats, 1);
    mi_option_set(mi_option_verbose, 1);
#endif

    ARKOSE_LOG(Verbose, "MemoryManagement: using mimalloc as default allocator (version {})", mimallocVersion);
#endif // WITH_MIMALLOC
}
ARK_ENABLE_OPTIMIZATIONS

void MemoryManager::shutdown()
{
#if WITH_MIMALLOC
#if defined(ARKOSE_DEBUG)
    ARKOSE_LOG(Info, "MemoryManagement: shutting down, mimalloc will now print stats");
#endif
#endif // WITH_MIMALLOC
}
