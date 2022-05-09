#pragma once

#include <vector>
#include <string>
#include <optional>

struct ShaderDefine {
    std::string symbol;
    std::optional<std::string> value;

    bool valid() const
    {
        return symbol.length() > 0;
    }

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

    bool isRayTracingShaderFile() const;

private:
    static ShaderFileType typeFromPath(const std::string&);

    std::string m_path;
    std::vector<ShaderDefine> m_defines;
    std::string m_defines_identifier;
    ShaderFileType m_type { ShaderFileType::Unknown };
};
