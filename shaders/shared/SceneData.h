#ifndef SCENE_DATA_H
#define SCENE_DATA_H

// These limits are arbitrary, and should be changed!
#define SCENE_MAX_DRAWABLES 128
#define SCENE_MAX_MATERIALS 64
#define SCENE_MAX_TEXTURES 256

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

#endif // SCENE_DATA_H
