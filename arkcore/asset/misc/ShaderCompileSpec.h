#pragma once

#include "rendering/ShaderStage.h"

#include <filesystem>
#include <string>
#include <vector>

struct ShaderCompileSpec {

    std::string shaderName;

    using ShaderFileSpec = std::pair<ShaderStage, std::filesystem::path>;
    std::vector<ShaderFileSpec> shaderFiles;

    using SymbolValuePair = std::pair<std::string, std::string>;
    using SymbolValuePairSet = std::vector<SymbolValuePair>;

    std::vector<SymbolValuePairSet> permutations;

    static std::unique_ptr<ShaderCompileSpec> loadFromFile(std::filesystem::path specPath);
};
