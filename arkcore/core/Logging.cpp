#include "Logging.h"

#include <atomic>

namespace Logging {

std::atomic_uint32_t LogWarningCounter {};
std::atomic_uint32_t LogErrorCounter {};

}
