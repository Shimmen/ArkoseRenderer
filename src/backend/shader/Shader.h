#pragma once

#include <string>
#include <vector>
#include <optional>
#include <unordered_map>

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

struct ShaderDefine {
    std::string symbol;
    std::optional<std::string> value;

    static ShaderDefine makeSymbol(std::string symbol)
    {
        ShaderDefine define;
        define.symbol = symbol;
        define.value = {};
        return define;
    }

    static ShaderDefine makeInt(std::string symbol, int intValue)
    {
        ShaderDefine define;
        define.symbol = symbol;
        define.value = std::to_string(intValue);
        return define;    
    }

    static ShaderDefine makeBool(std::string symbol, bool boolValue)
    {
        ShaderDefine define;
        define.symbol = symbol;
        define.value = boolValue ? "1" : "0";
        return define;
    }
};

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
    explicit ShaderFile(const std::string& path, std::initializer_list<ShaderDefine> = {});
    ShaderFile(std::string path, ShaderFileType, std::initializer_list<ShaderDefine> = {});

    [[nodiscard]] const std::string& path() const;
    [[nodiscard]] const std::vector<ShaderDefine>& defines() const;
    [[nodiscard]] const std::string& definesIdentifier() const;
    [[nodiscard]] ShaderFileType type() const;

private:
    static ShaderFileType typeFromPath(const std::string&);

    std::string m_path;
    std::vector<ShaderDefine> m_defines;
    std::string m_defines_identifier;
    ShaderFileType m_type { ShaderFileType::Unknown };
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