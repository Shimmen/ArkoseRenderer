#include "ShaderFile.h"

#include "rendering/backend/base/Backend.h"
#include "rendering/backend/shader/ShaderManager.h"
#include "core/Logging.h"
#include <algorithm>
#include <string>
#include <initializer_list>

#ifdef _WIN32
#include <cstdio>
#endif

ShaderFile::ShaderFile(std::filesystem::path path, std::vector<ShaderDefine> defines)
    : ShaderFile(std::move(path), stageFromPath(path), std::move(defines))
{
}

ShaderFile::ShaderFile(std::filesystem::path path, ShaderStage shaderStage, std::vector<ShaderDefine> defines)
    : m_path(std::move(path))
    , m_defines(std::move(defines))
    , m_shaderStage(shaderStage)
{
    if (isRayTracingShaderFile()) {
        // TODO: We want to get rid of Backend::get(). What should we do here?
        ShaderDefine rayTracingDefine = Backend::get().rayTracingShaderDefine();
        if (rayTracingDefine.valid()) {
            m_defines.push_back(rayTracingDefine);
        }
    }

    // Sort the list so we can assume that equivalent set of defines generates the same identifier
    std::sort(m_defines.begin(), m_defines.end(), [](const ShaderDefine& lhs, const ShaderDefine& rhs) {
        if (lhs.symbol == rhs.symbol) {
            return lhs.value < rhs.value;
        } else {
            return lhs.symbol < rhs.symbol;
        }
    });

    m_defines_identifier = "";
    for (size_t i = 0; i < m_defines.size(); ++i) {
        const ShaderDefine& define = m_defines[i];
        m_defines_identifier.append(define.symbol);
        if (define.value.has_value()) {
            m_defines_identifier.append("=");
            m_defines_identifier.append(define.value.value());
        }
        if (i < m_defines.size() - 1)
            m_defines_identifier.append(";");
    }

    ShaderManager::instance().registerShaderFile(*this);
}

const std::filesystem::path& ShaderFile::path() const
{
    return m_path;
}

const std::vector<ShaderDefine>& ShaderFile::defines() const
{
    return m_defines;
}

const std::string& ShaderFile::definesIdentifier() const
{
    return m_defines_identifier;
}

ShaderStage ShaderFile::shaderStage() const
{
    return m_shaderStage;
}

bool ShaderFile::valid() const
{
    return !m_path.empty() && m_shaderStage != ShaderStage::Unknown;
}

bool ShaderFile::isRayTracingShaderFile() const
{
    return isSet(shaderStage() & ShaderStage::AnyRayTrace);
}

ShaderStage ShaderFile::stageFromPath(std::filesystem::path const& path)
{
    if (!path.has_extension()) { 
        return ShaderStage::Unknown;
    }

    std::filesystem::path ext = path.extension();

    if (ext == ".vert")
        return ShaderStage::Vertex;
    else if (ext == ".frag")
        return ShaderStage::Fragment;
    else if (ext == ".rgen")
        return ShaderStage::RTRayGen;
    else if (ext == ".comp")
        return ShaderStage::Compute;
    else if (ext == ".rint")
        return ShaderStage::RTIntersection;
    else if (ext == ".rmiss")
        return ShaderStage::RTMiss;
    else if (ext == ".rchit")
        return ShaderStage::RTClosestHit;
    else if (ext == ".rahit")
        return ShaderStage::RTAnyHit;

    return ShaderStage::Unknown;
}
