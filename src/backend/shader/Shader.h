#pragma once

#include "backend/shader/ShaderFile.h"
#include <string>
#include <vector>
#include <optional>
#include <unordered_map>

// TODO: Make this an enum class with operator overloading for bit-patterns
enum ShaderStage : uint8_t {
    ShaderStageVertex = 0x01,
    ShaderStageFragment = 0x02,
    ShaderStageCompute = 0x04,
    ShaderStageRTRayGen = 0x08,
    ShaderStageRTMiss = 0x10,
    ShaderStageRTClosestHit = 0x20,
    ShaderStageRTAnyHit = 0x40,
    ShaderStageRTIntersection = 0x80,

    ShaderStageAnyRasterize = ShaderStageVertex | ShaderStageFragment,
    ShaderStageAnyRayTrace = ShaderStageRTRayGen | ShaderStageRTMiss | ShaderStageRTClosestHit | ShaderStageRTAnyHit | ShaderStageRTIntersection,
    ShaderStageAny = ShaderStageAnyRasterize | ShaderStageAnyRayTrace | ShaderStageCompute
};

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

    static Shader createVertexOnly(std::string vertexName, std::initializer_list<ShaderDefine> = {});
    static Shader createBasicRasterize(std::string vertexName, std::string fragmentName, std::initializer_list<ShaderDefine> = {});
    static Shader createCompute(std::string computeName, std::initializer_list<ShaderDefine> = {});

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
