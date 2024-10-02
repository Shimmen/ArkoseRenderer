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

ShaderFile::ShaderFile(const std::string& path, std::vector<ShaderDefine> defines)
    : ShaderFile(path, stageFromPath(path), std::move(defines))
{
}

ShaderFile::ShaderFile(std::string path, ShaderStage shaderStage, std::vector<ShaderDefine> defines)
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

const std::string& ShaderFile::path() const
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
    return m_path.length() > 0 && m_shaderStage != ShaderStage::Unknown;
}

bool ShaderFile::isRayTracingShaderFile() const
{
    return isSet(shaderStage() & ShaderStage::AnyRayTrace);
}

ShaderStage ShaderFile::stageFromPath(const std::string& path)
{
    if (path.length() < 5)
        return ShaderStage::Unknown;
    std::string ext5 = path.substr(path.length() - 5);

    if (ext5 == ".vert")
        return ShaderStage::Vertex;
    else if (ext5 == ".frag")
        return ShaderStage::Fragment;
    else if (ext5 == ".rgen")
        return ShaderStage::RTRayGen;
    else if (ext5 == ".comp")
        return ShaderStage::Compute;
    else if (ext5 == ".rint")
        return ShaderStage::RTIntersection;

    if (path.length() < 6)
        return ShaderStage::Unknown;
    std::string ext6 = path.substr(path.length() - 6);

    if (ext6 == ".rmiss")
        return ShaderStage::RTMiss;
    else if (ext6 == ".rchit")
        return ShaderStage::RTClosestHit;
    else if (ext6 == ".rahit")
        return ShaderStage::RTAnyHit;

    return ShaderStage::Unknown;
}
