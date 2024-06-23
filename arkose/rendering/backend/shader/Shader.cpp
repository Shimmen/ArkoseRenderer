#include "Shader.h"

Shader Shader::createVertexOnly(std::string vertexName, std::vector<ShaderDefine> defines)
{
    ShaderFile vertexFile { std::move(vertexName), ShaderStage::Vertex, defines };
    return Shader({ vertexFile }, ShaderType::Raster);
}

Shader Shader::createBasicRasterize(std::string vertexName, std::string fragmentName, std::vector<ShaderDefine> defines)
{
    ShaderFile vertexFile { std::move(vertexName), ShaderStage::Vertex, defines };
    ShaderFile fragmentFile { std::move(fragmentName), ShaderStage::Fragment, defines };
    return Shader({ vertexFile, fragmentFile }, ShaderType::Raster);
}

Shader Shader::createMeshShading(std::string taskName, std::string meshName, std::string fragmentName, std::vector<ShaderDefine> defines)
{
    ShaderFile taskFile { std::move(taskName), ShaderStage::Task, defines };
    ShaderFile meshFile { std::move(meshName), ShaderStage::Mesh, defines };
    ShaderFile fragmentFile { std::move(fragmentName), ShaderStage::Fragment, defines };
    return Shader({ taskFile, meshFile, fragmentFile }, ShaderType::Raster);
}

Shader Shader::createCompute(std::string computeName, std::vector<ShaderDefine> defines)
{
    ShaderFile computeFile { std::move(computeName), ShaderStage::Compute, defines };
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
