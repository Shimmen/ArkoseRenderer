#include "RayTracingState.h"

#include "rendering/Shader.h"
#include "utility/util.h"

HitGroup::HitGroup(ShaderFile closestHit, std::optional<ShaderFile> anyHit, std::optional<ShaderFile> intersection)
    : m_closestHit(closestHit)
    , m_anyHit(anyHit)
    , m_intersection(intersection)
{
    ASSERT(closestHit.type() == ShaderFileType::RTClosestHit);
    ASSERT(!anyHit.has_value() || anyHit.value().type() == ShaderFileType::RTAnyHit);
    ASSERT(!intersection.has_value() || intersection.value().type() == ShaderFileType::RTIntersection);
}

ShaderBindingTable::ShaderBindingTable(ShaderFile rayGen, std::vector<HitGroup> hitGroups, std::vector<ShaderFile> missShaders)
    : m_rayGen(rayGen)
    , m_hitGroups(std::move(hitGroups))
    , m_missShaders(std::move(missShaders))
{
    ASSERT(m_rayGen.type() == ShaderFileType::RTRaygen);
    ASSERT(!m_hitGroups.empty());
    for (const auto& miss : m_missShaders) {
        ASSERT(miss.type() == ShaderFileType::RTMiss);
    }

    m_pseudoShader = Shader(allReferencedShaderFiles(), ShaderType::RayTrace);
}

std::vector<ShaderFile> ShaderBindingTable::allReferencedShaderFiles() const
{
    std::vector<ShaderFile> files;

    files.push_back(rayGen());

    for (const HitGroup& hitGroup : hitGroups()) {
        files.push_back(hitGroup.closestHit());
        if (hitGroup.hasAnyHitShader()) {
            files.push_back(hitGroup.anyHit());
        }
        if (hitGroup.hasIntersectionShader()) {
            files.push_back(hitGroup.intersection());
        }
    }

    for (const ShaderFile& missShader : missShaders()) {
        files.push_back(missShader);
    }

    return files;
}


RayTracingState::RayTracingState(Backend& backend, ShaderBindingTable sbt, const StateBindings& stateBindings, uint32_t maxRecursionDepth)
    : Resource(backend)
    , m_shaderBindingTable(sbt)
    , m_stateBindings(stateBindings)
    , m_maxRecursionDepth(maxRecursionDepth)
{
}

uint32_t RayTracingState::maxRecursionDepth() const
{
    return m_maxRecursionDepth;
}

const ShaderBindingTable& RayTracingState::shaderBindingTable() const
{
    return m_shaderBindingTable;
}
