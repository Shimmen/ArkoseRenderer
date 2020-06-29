#pragma once

#include <string>
#include <vector>

enum class ShaderFileType {
    Vertex,
    Fragment,
    Compute,
    RTRaygen,
    RTClosestHit,
    RTAnyHit,
    RTIntersection,
    RTMiss,
    Unknown,
};

struct ShaderFile {
    ShaderFile() = default;
    explicit ShaderFile(std::string path);
    ShaderFile(std::string path, ShaderFileType);

    [[nodiscard]] const std::string& path() const;
    [[nodiscard]] ShaderFileType type() const;

    static ShaderFileType shaderFileTypeFromPath(const std::string&);

private:
    std::string m_path;
    ShaderFileType m_type { ShaderFileType::Unknown };
};

enum class ShaderType {
    Raster,
    Compute,
    RayTrace,
};

struct Shader {

    static Shader createVertexOnly(std::string vertexName);
    static Shader createBasic(std::string vertexName, std::string fragmentName);
    static Shader createCompute(std::string computeName);

    Shader() = default;
    Shader(std::vector<ShaderFile>, ShaderType type);
    ~Shader();

    [[nodiscard]] ShaderType type() const;
    [[nodiscard]] const std::vector<ShaderFile>& files() const;

    // TODO: We should maybe add some utility API for shader introspection here..?
    //  Somehow we need to extract descriptor sets etc.
    //  but maybe that is backend-specific or file specific?

private:
    std::vector<ShaderFile> m_files {};
    ShaderType m_type {};
};
