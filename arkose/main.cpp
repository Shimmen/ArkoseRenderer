
//
// NOTE: This is the entry point / main function for some platforms, but not all!
//       Certain platforms require abnormal main functions (e.g. other function signature)
//       or some other special setup. If it's not this one, you should be able to find the
//       entry point for your system or platform of interest under `arkose/system/<name>/*`.
//

#include "application/Arkose.h"

#if defined(ARKOSE_USE_DEFAULT_MAIN)
int main(int argc, char** argv)
{
    return Arkose::runArkoseApplication(argc, argv);
}
#endif
