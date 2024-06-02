#pragma once

#include "core/Types.h"
#include "rendering/backend/shader/ShaderStage.h"
#include <string>

struct NamedConstant {
    std::string name {};
    std::string type {};
    u32 size { 0 };
    u32 offset { 0 };
    ShaderStage stages {};
};
