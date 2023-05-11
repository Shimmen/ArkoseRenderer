#pragma once

#include "rendering/BlendMode.h"
#include <optional>

class MaterialAsset;

// TODO: Remove me and define somewhere else.. like in the material asset, once we actually handle it.
enum class Brdf {
    GgxMicrofacet = 0,
};

class DrawKey {
public:
    DrawKey(std::optional<Brdf>, std::optional<BlendMode>, std::optional<bool> doubleSided);
    DrawKey() = default;
    ~DrawKey() = default;

    static DrawKey generate(MaterialAsset*);

    std::optional<Brdf> brdf() const { return m_brdf; }
    std::optional<BlendMode> blendMode() const { return m_blendMode; }
    std::optional<bool> doubleSided() const { return m_doubleSided; }

    u32 asUint32() const;

private:
    std::optional<Brdf> m_brdf {};
    std::optional<BlendMode> m_blendMode {};
    std::optional<bool> m_doubleSided {};
};
