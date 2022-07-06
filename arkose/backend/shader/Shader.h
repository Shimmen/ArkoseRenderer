#pragma once

#include "backend/shader/ShaderFile.h"
#include "utility/EnumHelpers.h"
#include <string>
#include <vector>
#include <optional>
#include <unordered_map>

enum class ShaderStage {
    Vertex = 0x01,
    Fragment = 0x02,
    Compute = 0x04,
    RTRayGen = 0x08,
    RTMiss = 0x10,
    RTClosestHit = 0x20,
    RTAnyHit = 0x40,
    RTIntersection = 0x80,

    AnyRasterize = Vertex | Fragment,
    AnyRayTrace = RTRayGen | RTMiss | RTClosestHit | RTAnyHit | RTIntersection,
    Any = AnyRasterize | AnyRayTrace | Compute
};
ARKOSE_ENUM_CLASS_BIT_FLAGS(ShaderStage)

enum class ShaderType {
    Raster,
    Compute,
    RayTrace,
};

struct Shader {

    struct UniformBinding {
        // TODO: Include type information for extra safety?
        ShaderStage stages;
        //std::vector<ShaderFileType> stages; // TODO: Use ShaderStage!
        uint32_t offset;
        uint32_t size;
    };

    static Shader createVertexOnly(std::string vertexName, std::vector<ShaderDefine> = {});
    static Shader createBasicRasterize(std::string vertexName, std::string fragmentName, std::vector<ShaderDefine> = {});
    static Shader createCompute(std::string computeName, std::vector<ShaderDefine> = {});

    Shader() = default;
    Shader(std::vector<ShaderFile>, ShaderType type);

    [[nodiscard]] ShaderType type() const;
    [[nodiscard]] const std::vector<ShaderFile>& files() const;

    std::optional<UniformBinding> uniformBindingForName(const std::string&) const;
    
    bool hasUniformBindingsSetup() const { return m_uniformBindingsSet; }
    void setUniformBindings(std::unordered_map<std::string, UniformBinding>);

private:
    std::vector<ShaderFile> m_files {};
    ShaderType m_type {};

    // TODO: If shaders are created through the Registry we don't need this flag and can simply set it up when it's created!
    bool m_uniformBindingsSet { false };
    std::unordered_map<std::string, UniformBinding> m_uniformBindings {};
};
