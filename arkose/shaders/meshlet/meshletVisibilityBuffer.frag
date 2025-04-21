#version 460

#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require

#include <common/material.glsl>
#include <common/namedUniforms.glsl>
#include <meshlet/meshletCommon.glsl>
#include <shared/SceneData.h>
#include <shared/ShaderBlendMode.h>

#ifndef VISBUF_DEPTH_ONLY
layout(location = 0) flat in uint vDrawableIdx;
layout(location = 1) flat in uint vMeshletIdx;
layout(location = 2) flat in meshlet_rel_idx_t vPrimitiveIdx;
#endif
#if VISBUF_BLEND_MODE == BLEND_MODE_MASKED
layout(location = 3) flat in uint vMaterialIdx;
layout(location = 4) in vec2 vTexCoord;
#endif

#if VISBUF_BLEND_MODE == BLEND_MODE_MASKED
DeclareCommonBindingSet_Material(3)
#endif

#ifndef VISBUF_DEPTH_ONLY
layout(location = 0) out uint oInstanceVisibilityData;
layout(location = 1) out uint oTriangleVisibilityData;
#endif

void main()
{
#if VISBUF_BLEND_MODE == BLEND_MODE_MASKED
    ShaderMaterial material = material_getMaterial(vMaterialIdx);
    vec4 inputBaseColor = texture(material_getTexture(material.baseColor), vTexCoord).rgba;
    float mask = inputBaseColor.a;
    if (mask < material.maskCutoff) {
        discard;
    }
#endif

#ifndef VISBUF_DEPTH_ONLY
    oInstanceVisibilityData = vDrawableIdx + 1;
    oTriangleVisibilityData = ((vMeshletIdx + 1) << 8) | uint(vPrimitiveIdx);
#endif
}
