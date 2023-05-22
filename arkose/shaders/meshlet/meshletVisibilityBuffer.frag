#version 460

#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require

#include <common/namedUniforms.glsl>
#include <shared/SceneData.h>
#include <shared/ShaderBlendMode.h>

layout(location = 0) flat in uint vDrawableIdx;
layout(location = 1) flat in uint vMeshletIdx;
layout(location = 2) flat in uint8_t vPrimitiveIdx;
#if VISBUF_BLEND_MODE == BLEND_MODE_MASKED
layout(location = 3) flat in uint vMaterialIdx;
layout(location = 4) in vec2 vTexCoord;
#endif

#if VISBUF_BLEND_MODE == BLEND_MODE_MASKED
layout(set = 3, binding = 0) buffer readonly MaterialBlock { ShaderMaterial materials[]; };
layout(set = 3, binding = 1) uniform sampler2D textures[];
#endif

layout(location = 0) out uint oInstanceVisibilityData;
layout(location = 1) out uint oTriangleVisibilityData;

void main()
{
#if VISBUF_BLEND_MODE == BLEND_MODE_MASKED
    ShaderMaterial material = materials[vMaterialIdx];
    vec4 inputBaseColor = texture(textures[nonuniformEXT(material.baseColor)], vTexCoord).rgba;
    float mask = inputBaseColor.a;
    if (mask < material.maskCutoff) {
        discard;
    }
#endif

    oInstanceVisibilityData = vDrawableIdx + 1;
    oTriangleVisibilityData = ((vMeshletIdx + 1) << 8) | uint(vPrimitiveIdx);
}
