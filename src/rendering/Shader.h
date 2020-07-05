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
    /* implicit */ ShaderFile(const std::string& path); // NOLINT(google-explicit-constructor)
    ShaderFile(std::string path, ShaderFileType);

    [[nodiscard]] const std::string& path() const;
    [[nodiscard]] ShaderFileType type() const;

    static ShaderFileType typeFromPath(const std::string&);

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
    static Shader createBasicRasterize(std::string vertexName, std::string fragmentName);
    static Shader createCompute(std::string computeName);

    Shader() = default;
    Shader(std::vector<ShaderFile>, ShaderType type);

    [[nodiscard]] ShaderType type() const;
    [[nodiscard]] const std::vector<ShaderFile>& files() const;

private:
    std::vector<ShaderFile> m_files {};
    ShaderType m_type {};
};
