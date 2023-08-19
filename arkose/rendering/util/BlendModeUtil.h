#pragma once

#include "core/Types.h"
#include "rendering/BlendMode.h"
#include "shaders/shared/ShaderBlendMode.h"

inline u32 blendModeToShaderBlendMode(BlendMode blendMode)
{
    switch (blendMode) {
    case BlendMode::Opaque:
        return BLEND_MODE_OPAQUE;
    case BlendMode::Masked:
        return BLEND_MODE_MASKED;
    case BlendMode::Translucent:
        return BLEND_MODE_TRANSLUCENT;
    default:
        ASSERT_NOT_REACHED();
    }
}
