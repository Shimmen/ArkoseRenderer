#pragma once

#include "rendering/backend/Resource.h"
#include "rendering/backend/util/StateBindings.h"

class HitGroup {
public:
    HitGroup();
    explicit HitGroup(ShaderFile closestHit, std::optional<ShaderFile> anyHit = {}, std::optional<ShaderFile> intersection = {});

    const ShaderFile& closestHit() const { return m_closestHit; }

    bool hasAnyHitShader() const { return m_anyHit.has_value(); }
    const ShaderFile& anyHit() const { return m_anyHit.value(); }

    bool hasIntersectionShader() const { return m_intersection.has_value(); }
    const ShaderFile& intersection() const { return m_intersection.value(); }

    bool valid() const;

private:
    ShaderFile m_closestHit {};
    std::optional<ShaderFile> m_anyHit {};
    std::optional<ShaderFile> m_intersection {};
};

class ShaderBindingTable {
public:
    // See https://www.willusher.io/graphics/2019/11/20/the-sbt-three-ways for all info you might want about SBT stuff!
    // TODO: Add support for ShaderRecord instead of just shader file, so we can include parameters to the records.

    ShaderBindingTable() = default;
    ShaderBindingTable(ShaderFile rayGen, std::vector<HitGroup> hitGroups, std::vector<ShaderFile> missShaders);

    void setRayGenerationShader(ShaderFile);
    void setMissShader(u32 index, ShaderFile);
    void setHitGroup(u32 index, HitGroup);

    const ShaderFile& rayGen() const { return m_rayGen; }
    const std::vector<HitGroup>& hitGroups() const { return m_hitGroups; }
    const std::vector<ShaderFile>& missShaders() const { return m_missShaders; }

    std::vector<ShaderFile> allReferencedShaderFiles() const;
    Shader const& pseudoShader() const;

private:
    // TODO: In theory we can have more than one ray gen shader!
    ShaderFile m_rayGen;
    std::vector<HitGroup> m_hitGroups;
    std::vector<ShaderFile> m_missShaders;

    // A shader which is simply a collection of all used shader files
    mutable Shader m_pseudoShader;
};

class RayTracingState : public Resource {
public:
    RayTracingState() = default;
    RayTracingState(Backend&, ShaderBindingTable, const StateBindings&, uint32_t maxRecursionDepth);

    [[nodiscard]] uint32_t maxRecursionDepth() const;
    [[nodiscard]] const ShaderBindingTable& shaderBindingTable() const;
    const StateBindings& stateBindings() const { return m_stateBindings; }

private:
    ShaderBindingTable m_shaderBindingTable;
    StateBindings m_stateBindings;
    uint32_t m_maxRecursionDepth;
};
