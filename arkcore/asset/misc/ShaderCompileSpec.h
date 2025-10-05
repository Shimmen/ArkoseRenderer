#pragma once

#include <filesystem>
#include <string>
#include <vector>

struct ShaderCompileSpec {

    std::string shaderName;

    std::filesystem::path vertexShaderFile;
    std::filesystem::path fragmentShaderFile;

    std::filesystem::path raygenShaderFile;
    std::vector<std::filesystem::path> closestHitShaderFiles;
    std::vector<std::filesystem::path> anyHitShaderFiles;
    std::vector<std::filesystem::path> missShaderFiles;
    std::vector<std::filesystem::path> intersectionShaderFiles;

    std::filesystem::path computeShaderFile;

    using SymbolValuePair = std::pair<std::string, std::string>;
    using SymbolValuePairSet = std::vector<SymbolValuePair>;

    std::vector<SymbolValuePairSet> permutations;

    static std::unique_ptr<ShaderCompileSpec> loadFromFile(std::filesystem::path specPath);
};
