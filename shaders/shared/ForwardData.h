#ifndef FORWARD_DATA_H
#define FORWARD_DATA_H

#define FORWARD_MAX_DRAWABLES 128
#define FORWARD_MAX_MATERIALS 64
#define FORWARD_MAX_TEXTURES 256

struct ForwardMaterial {
    int baseColor;
    int normalMap;
    int pad0, pad1;
};

struct PerForwardObject {
    mat4 worldFromLocal;
    mat4 worldFromTangent;
    int materialIndex;
    int pad1, pad2, pad3;
};

#endif // FORWARD_DATA_H
