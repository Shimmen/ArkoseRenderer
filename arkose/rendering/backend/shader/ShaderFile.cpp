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
    : ShaderFile(path, typeFromPath(path), std::move(defines))
{
}

ShaderFile::ShaderFile(std::string path, ShaderFileType type, std::vector<ShaderDefine> defines)
    : m_path(std::move(path))
    , m_defines(std::move(defines))
    , m_type(type)
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
    for (int i = 0; i < m_defines.size(); ++i) {
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

ShaderFileType ShaderFile::type() const
{
    return m_type;
}

bool ShaderFile::valid() const
{
    return m_path.length() > 0 && m_type != ShaderFileType::Unknown;
}

bool ShaderFile::isRayTracingShaderFile() const
{
    switch (type()) {
    case ShaderFileType::RTAnyHit:
    case ShaderFileType::RTClosestHit:
    case ShaderFileType::RTIntersection:
    case ShaderFileType::RTMiss:
    case ShaderFileType::RTRayGen:
        return true;
    default:
        return false;
    }
}

ShaderFileType ShaderFile::typeFromPath(const std::string& path)
{
    if (path.length() < 5)
        return ShaderFileType::Unknown;
    std::string ext5 = path.substr(path.length() - 5);

    if (ext5 == ".vert")
        return ShaderFileType::Vertex;
    else if (ext5 == ".frag")
        return ShaderFileType::Fragment;
    else if (ext5 == ".rgen")
        return ShaderFileType::RTRayGen;
    else if (ext5 == ".comp")
        return ShaderFileType::Compute;
    else if (ext5 == ".rint")
        return ShaderFileType::RTIntersection;

    if (path.length() < 6)
        return ShaderFileType::Unknown;
    std::string ext6 = path.substr(path.length() - 6);

    if (ext6 == ".rmiss")
        return ShaderFileType::RTMiss;
    else if (ext6 == ".rchit")
        return ShaderFileType::RTClosestHit;
    else if (ext6 == ".rahit")
        return ShaderFileType::RTAnyHit;

    return ShaderFileType::Unknown;
}
