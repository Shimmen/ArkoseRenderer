#include "Shader.h"

#include "rendering/ShaderManager.h"
#include <utility/Logging.h>

ShaderFile::ShaderFile(const std::string& path)
    : ShaderFile(path, typeFromPath(path))
{
}

ShaderFile::ShaderFile(std::string path, ShaderFileType type)
    : m_path(std::move(path))
    , m_type(type)
{
    auto maybeError = ShaderManager::instance().loadAndCompileImmediately(m_path);
    if (maybeError.has_value()) {
        LogError("Shader file error: %s\n", maybeError.value().c_str());
        LogErrorAndExit("Exiting due to bad shader at startup.\n");
    }
}

const std::string& ShaderFile::path() const
{
    return m_path;
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

Shader Shader::createVertexOnly(std::string vertexName)
{
    ShaderFile vertexFile { std::move(vertexName), ShaderFileType::Vertex };
    return Shader({ vertexFile }, ShaderType::Raster);
}

Shader Shader::createBasicRasterize(std::string vertexName, std::string fragmentName)
{
    ShaderFile vertexFile { std::move(vertexName), ShaderFileType::Vertex };
    ShaderFile fragmentFile { std::move(fragmentName), ShaderFileType::Fragment };
    return Shader({ vertexFile, fragmentFile }, ShaderType::Raster);
}

Shader Shader::createCompute(std::string computeName)
{
    ShaderFile computeFile { std::move(computeName), ShaderFileType::Compute };
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
