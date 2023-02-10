#include "RayTracingState.h"

#include "rendering/backend/shader/Shader.h"
#include "core/Assert.h"

HitGroup::HitGroup(ShaderFile closestHit, std::optional<ShaderFile> anyHit, std::optional<ShaderFile> intersection)
    : m_closestHit(closestHit)
    , m_anyHit(anyHit)
    , m_intersection(intersection)
{
    ARKOSE_ASSERT(closestHit.type() == ShaderFileType::RTClosestHit);
    ARKOSE_ASSERT(!anyHit.has_value() || anyHit.value().type() == ShaderFileType::RTAnyHit);
    ARKOSE_ASSERT(!intersection.has_value() || intersection.value().type() == ShaderFileType::RTIntersection);
}

ShaderBindingTable::ShaderBindingTable(ShaderFile rayGen, std::vector<HitGroup> hitGroups, std::vector<ShaderFile> missShaders)
    : m_rayGen(rayGen)
    , m_hitGroups(std::move(hitGroups))
    , m_missShaders(std::move(missShaders))
{
    ARKOSE_ASSERT(m_rayGen.type() == ShaderFileType::RTRaygen);
    for (const auto& miss : m_missShaders) {
        ARKOSE_ASSERT(miss.type() == ShaderFileType::RTMiss);
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
