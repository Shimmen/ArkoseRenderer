#pragma once

#include "rendering/backend/shader/ShaderStage.h"
#include <filesystem>
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

struct ShaderFile {
    ShaderFile() = default;
    explicit ShaderFile(std::filesystem::path path, std::vector<ShaderDefine> = {});
    ShaderFile(std::filesystem::path path, ShaderStage, std::vector<ShaderDefine> = {});

    [[nodiscard]] const std::filesystem::path& path() const;
    [[nodiscard]] const std::vector<ShaderDefine>& defines() const;
    [[nodiscard]] const std::string& definesIdentifier() const;
    [[nodiscard]] ShaderStage shaderStage() const;

    bool valid() const;

    bool isRayTracingShaderFile() const;

private:
    static ShaderStage stageFromPath(std::filesystem::path const&);

    std::filesystem::path m_path;
    std::vector<ShaderDefine> m_defines;
    std::string m_defines_identifier;
    ShaderStage m_shaderStage { ShaderStage::Unknown };
};
