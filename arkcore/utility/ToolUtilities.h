#pragma once

#include "core/Assert.h"
#include "core/Logging.h"

int toolReturnCode()
{
    if (ArkoseAssertionCounter > 0 || Logging::LogErrorCounter > 0) {
        return 1;
    } else if (Logging::LogWarningCounter > 0) {
        return -1;
    } else {
        return 0;
    }
}
