#pragma once

#include "rendering/BlendMode.h"

class MaterialAsset;

// TODO: Remove me and define somewhere else.. like in the material asset, once we actually handle it.
enum class Brdf {
    GgxMicrofacet = 0,
};

class DrawKey {
public:
    DrawKey(Brdf, BlendMode, bool doubleSided);
    DrawKey() = default;
    ~DrawKey() = default;

    static DrawKey generate(MaterialAsset*);

    u32 asUint32() const;

private:
    Brdf m_brdf { Brdf::GgxMicrofacet };
    BlendMode m_blendMode { BlendMode::Opaque };
    bool m_doubleSided { false };
};
