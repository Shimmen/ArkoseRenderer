#include "Shader.h"

#include "rendering/ShaderManager.h"
#include <utility/Logging.h>

#ifdef _WIN32
#include <cstdio>
#endif

ShaderFile::ShaderFile(const std::string& path, std::initializer_list<ShaderDefine> defines)
    : ShaderFile(path, typeFromPath(path), std::move(defines))
{
}

ShaderFile::ShaderFile(std::string path, ShaderFileType type, std::initializer_list<ShaderDefine> defines)
    : m_path(std::move(path))
    , m_defines(std::move(defines))
    , m_type(type)
{
    //m_defines_identifier = createIdentifierForDefines();
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

    std::optional<std::string> maybeError = {};
    do {
        maybeError = ShaderManager::instance().loadAndCompileImmediately(*this);
        if (maybeError.has_value()) {
            LogError("Shader file error: %s\n", maybeError.value().c_str());
            #ifdef _WIN32
                LogError("Edit & and save the shader, then ...\n");
                system("pause");
            #else
                LogErrorAndExit("Exiting due to bad shader at startup.\n");
            #endif
        }
    } while (maybeError.has_value());
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
        return ShaderFileType::RTRaygen;
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

Shader Shader::createVertexOnly(std::string vertexName, std::initializer_list<ShaderDefine> defines)
{
    ShaderFile vertexFile { std::move(vertexName), ShaderFileType::Vertex, defines };
    return Shader({ vertexFile }, ShaderType::Raster);
}

Shader Shader::createBasicRasterize(std::string vertexName, std::string fragmentName, std::initializer_list<ShaderDefine> defines)
{
    ShaderFile vertexFile { std::move(vertexName), ShaderFileType::Vertex, defines };
    ShaderFile fragmentFile { std::move(fragmentName), ShaderFileType::Fragment, defines };
    return Shader({ vertexFile, fragmentFile }, ShaderType::Raster);
}

Shader Shader::createCompute(std::string computeName, std::initializer_list<ShaderDefine> defines)
{
    ShaderFile computeFile { std::move(computeName), ShaderFileType::Compute, defines };
    return Shader({ computeFile }, ShaderType::Compute);
}

Shader::Shader(std::vector<ShaderFile> files, ShaderType type)
    : m_files(std::move(files))
    , m_type(type)
{
}

ShaderType Shader::type() const
{
    return m_type;
}

const std::vector<ShaderFile>& Shader::files() const
{
    return m_files;
}

std::optional<Shader::UniformBinding> Shader::uniformBindingForName(const std::string& name) const
{
    auto entry = m_uniformBindings.find(name);
    if (entry == m_uniformBindings.end()) {
        return {};
    }

    const Shader::UniformBinding& binding = entry->second;
    return binding;
}

void Shader::setUniformBindings(std::unordered_map<std::string, Shader::UniformBinding> bindings)
{
    ASSERT(m_uniformBindingsSet == false);
    m_uniformBindings = std::move(bindings);
    m_uniformBindingsSet = true;
}
