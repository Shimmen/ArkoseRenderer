#include "Shader.h"

#include "backend/shader/ShaderManager.h"
#include "core/Assert.h"
#include "core/Logging.h"

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
    ARKOSE_ASSERT(m_uniformBindingsSet == false);
    m_uniformBindings = std::move(bindings);
    m_uniformBindingsSet = true;
}