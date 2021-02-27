#ifndef SCENE_DATA_H
#define SCENE_DATA_H

// These limits are arbitrary, and should be changed! Actually, we should try to not have any weird arbitrary limits..
#define SCENE_MAX_DRAWABLES 128
#define SCENE_MAX_MATERIALS 128
#define SCENE_MAX_TEXTURES 256
#define SCENE_MAX_SHADOW_MAPS 16

struct ShaderDrawable {
    mat4 worldFromLocal;
    mat4 worldFromTangent;
    int materialIndex;
    int pad1, pad2, pad3;
};

struct ShaderMaterial {
    int baseColor;
    int normalMap;
    int metallicRoughness;
    int emissive;
};

#ifdef __cplusplus
constexpr bool operator==(const ShaderMaterial& lhs, const ShaderMaterial& rhs)
{
    if (lhs.baseColor != rhs.baseColor)
        return false;
    if (lhs.normalMap != rhs.normalMap)
        return false;
    if (lhs.metallicRoughness != rhs.metallicRoughness)
        return false;
    if (lhs.emissive != rhs.emissive)
        return false;
    return true;
}
#endif

#endif // SCENE_DATA_H
