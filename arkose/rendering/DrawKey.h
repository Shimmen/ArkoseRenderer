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
    DrawKey(std::optional<Brdf>, std::optional<BlendMode>, std::optional<bool> doubleSided, std::optional<bool> hasExplicitVelocity);
    DrawKey() = default;
    ~DrawKey() = default;

    static DrawKey generate(MaterialAsset*);

    std::optional<Brdf> brdf() const { return m_brdf; }
    std::optional<BlendMode> blendMode() const { return m_blendMode; }
    std::optional<bool> doubleSided() const { return m_doubleSided; }

    std::optional<bool> hasExplicityVelocity() const { return m_hasExplicitVelocity; }
    void setHasExplicityVelocity(bool value) { m_hasExplicitVelocity = value; }

    u32 asUint32() const;

    bool operator==(DrawKey const&) const;

    static std::vector<DrawKey> createCompletePermutationSet();
    static size_t calculateCompletePermutationSetCount();

private:
    std::optional<Brdf> m_brdf {};
    std::optional<BlendMode> m_blendMode {};
    std::optional<bool> m_doubleSided {};
    std::optional<bool> m_hasExplicitVelocity {};
};
