#include "RayTracingState.h"

#include "rendering/backend/shader/Shader.h"
#include "core/Assert.h"

HitGroup::HitGroup() = default;

HitGroup::HitGroup(ShaderFile closestHit, std::optional<ShaderFile> anyHit, std::optional<ShaderFile> intersection)
    : m_closestHit(closestHit)
    , m_anyHit(anyHit)
    , m_intersection(intersection)
{
    ARKOSE_ASSERT(closestHit.type() == ShaderFileType::RTClosestHit);
    ARKOSE_ASSERT(!anyHit.has_value() || anyHit.value().type() == ShaderFileType::RTAnyHit);
    ARKOSE_ASSERT(!intersection.has_value() || intersection.value().type() == ShaderFileType::RTIntersection);
}

bool HitGroup::valid() const
{
    return m_closestHit.valid();
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

void ShaderBindingTable::setRayGenerationShader(ShaderFile rayGenerationShader)
{
    ARKOSE_ASSERT(rayGenerationShader.type() == ShaderFileType::RTRaygen);

    ARKOSE_ASSERT(!m_rayGen.valid());
    m_rayGen = std::move(rayGenerationShader);
}

void ShaderBindingTable::setMissShader(u32 index, ShaderFile missShader)
{
    ARKOSE_ASSERT(missShader.type() == ShaderFileType::RTMiss);

    if (index >= m_missShaders.size()) {
        m_missShaders.resize(index + 1);
    }

    ARKOSE_ASSERT(!m_missShaders[index].valid());
    m_missShaders[index] = std::move(missShader);
}

void ShaderBindingTable::setHitGroup(u32 index, HitGroup hitGroup)
{
    if (index >= m_hitGroups.size()) {
        m_hitGroups.resize(index + 1);
    }

    ARKOSE_ASSERT(!m_hitGroups[index].valid());
    m_hitGroups[index] = std::move(hitGroup);
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

Shader const& ShaderBindingTable::pseudoShader() const
{
    if (m_pseudoShader.files().size() == 0) {
        m_pseudoShader = Shader(allReferencedShaderFiles(), ShaderType::RayTrace);
    }

    return m_pseudoShader;
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
