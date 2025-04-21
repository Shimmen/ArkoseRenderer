#pragma once

#include "rendering/backend/shader/ShaderFile.h"
#include <string>
#include <vector>

enum class ShaderType {
    Raster,
    Compute,
    RayTrace,
};

struct Shader {
    static Shader createVertexOnly(std::string vertexName, std::vector<ShaderDefine> = {});
    static Shader createBasicRasterize(std::string vertexName, std::string fragmentName, std::vector<ShaderDefine> = {});
    static Shader createMeshShading(std::string taskName, std::string meshName, std::string fragmentName, std::vector<ShaderDefine> = {});
    static Shader createMeshShading(std::string taskName, std::string meshName, std::vector<ShaderDefine> = {});
    static Shader createCompute(std::string computeName, std::vector<ShaderDefine> = {});

    Shader() = default;
    Shader(std::vector<ShaderFile>, ShaderType type);

    [[nodiscard]] ShaderType type() const;
    [[nodiscard]] const std::vector<ShaderFile>& files() const;

private:
    std::vector<ShaderFile> m_files {};
    ShaderType m_type {};
};
