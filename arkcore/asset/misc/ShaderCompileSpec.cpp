#include "ShaderCompileSpec.h"

#include "core/Assert.h"
#include "core/Logging.h"
#include "utility/FileIO.h"

#include <toml++/toml.hpp>
#include <vector>

using namespace std::literals;

// until c++23 lands with std::ranges::cartesian_product
template<typename T>
static std::vector<std::vector<T>> cartesianProduct(const std::vector<std::vector<T>>& v)
{
    std::vector<std::vector<T>> result = { {} };

    for (auto const& subset : v) {
        std::vector<std::vector<T>> temp;
        for (auto const& prefix : result) {
            for (auto const& value : subset) {
                auto newCombination = prefix;
                newCombination.push_back(value);
                temp.push_back(std::move(newCombination));
            }
        }

        result = std::move(temp);
    }

    return result;
}

std::unique_ptr<ShaderCompileSpec> ShaderCompileSpec::loadFromFile(std::filesystem::path specPath)
{
    if (!FileIO::fileReadable(specPath)) {
        ARKOSE_LOG(Error, "ShaderCompileSpec: can't read shader spec file '{}'", specPath);
        return nullptr;
    }

    auto compileSpecToml = toml::parse_file(specPath.c_str());
    auto compileSpec = std::make_unique<ShaderCompileSpec>();

    compileSpec->shaderName = compileSpecToml["shader"sv].value_or(""sv);
    ARKOSE_LOG(Info, "ShaderCompileSpec: compiling shader '{}'", compileSpec->shaderName);

    // Parsing shader files

    auto shaderFiles = compileSpecToml["file"sv].as_table();
    if (shaderFiles == nullptr) {
        ARKOSE_LOG(Error, "ShaderCompileSpec: no 'file's listed in shader spec");
        return nullptr;
    }

    ARKOSE_LOG(Info, "ShaderCompileSpec: found {} shader files:", shaderFiles->size());
    for (const auto& [key, value] : *shaderFiles) {
        std::string_view fileType = key.data();
        std::string_view filePath = value.value_or(""sv);
        ARKOSE_LOG(Info, "ShaderCompileSpec:  {} shader '{}'", fileType, filePath);

        if (fileType == "vertex"sv) {
            compileSpec->vertexShaderFile = filePath;
        } else if (fileType == "fragment"sv) {
            compileSpec->fragmentShaderFile = filePath;
        } else if (fileType == "compute"sv) {
            compileSpec->computeShaderFile = filePath;
        } else if (fileType == "raygen"sv) {
            compileSpec->raygenShaderFile = filePath;
        } else if (fileType == "closesthit"sv) {
            // TODO: Parse an array!
            compileSpec->closestHitShaderFiles.push_back(filePath);
        } else if (fileType == "anyhit"sv) {
            // TODO: Parse an array!
            compileSpec->anyHitShaderFiles.push_back(filePath);
        } else if (fileType == "miss"sv) {
            // TODO: Parse an array!
            compileSpec->missShaderFiles.push_back(filePath);
        } else if (fileType == "intersection"sv) {
            // TODO: Parse an array!
            compileSpec->intersectionShaderFiles.push_back(filePath);
        } else {
            ARKOSE_LOG(Warning, "ShaderCompileSpec:   unknown shader type '{}', skipping", fileType);
        }
    }

    // Parsing options

    struct ShaderOption {
        std::string symbol;
        std::vector<std::string> values;
    };

    std::vector<ShaderOption> shaderOptions {};

    auto shaderOptionsTable = compileSpecToml["option"sv].as_table();
    if (shaderOptionsTable) {
        ARKOSE_LOG(Info, "ShaderCompileSpec: found {} shader options:", shaderOptionsTable->size());
        for (const auto& [key, value] : *shaderOptionsTable) {

            std::string_view optionName = key.data();
            ARKOSE_LOG(Info, "ShaderCompileSpec:  option '{}'", optionName);

            if (!value.is_table()) {
                ARKOSE_LOG(Warning, "ShaderCompileSpec:   option '{}' is not a table, skipping", optionName);
                continue;
            }

            auto& optionInfo = *value.as_table();

            ShaderOption& shaderOption = shaderOptions.emplace_back();
            shaderOption.symbol = optionInfo["symbol"sv].value_or(""sv);

            std::string_view optionType = optionInfo["type"sv].value_or(""sv);
            if (optionType == "bool"sv) {
                shaderOption.values.push_back("0");
                shaderOption.values.push_back("1");
                ARKOSE_LOG(Verbose, "ShaderCompileSpec:   bool option '{}' with symbol '{}'", optionName, shaderOption.symbol);
            } else if (optionType == "enum"sv) {
                std::string_view enumName = optionInfo["enum"sv].value_or(""sv);
                if (enumName == "BlendMode"sv) {
                    ARKOSE_LOG(Verbose, "ShaderCompileSpec:   enum option '{}' with symbol '{}' and enum type '{}'", optionName, shaderOption.symbol, enumName);
                    // TODO: Can we reflect this from code somehow? Would be nice to not have to match up all the different truths..
                    shaderOption.values.push_back("BLEND_MODE_OPAQUE");
                    shaderOption.values.push_back("BLEND_MODE_MASKED");
                    shaderOption.values.push_back("BLEND_MODE_TRANSLUCENT");
                } else {
                    ARKOSE_LOG(Warning, "ShaderCompileSpec:   option '{}' has unknown enum type '{}', skipping", optionName, enumName);
                    shaderOptions.pop_back();
                    continue;
                }
            } else {
                ARKOSE_LOG(Warning, "ShaderCompileSpec:   option '{}' has unknown type '{}', skipping", optionName, optionType);
                shaderOptions.pop_back();
                continue;
            }
        }
    } else {
        ARKOSE_LOG(Verbose, "ShaderCompileSpec: no 'option's listed in shader spec");
    }

    //
    // Construct all permutations
    //

    size_t numPermutations = 1;
    for (ShaderOption const& shaderOption : shaderOptions) {
        numPermutations *= shaderOption.values.size();
    }

    ARKOSE_LOG(Info, "ShaderCompileSpec: will compile a total of {} permutations", numPermutations);

    // Expand each option into a list of (symbol, value) pairs

    using SymbolValuePair = std::pair<std::string, std::string>;
    using SymbolValuePairSet = std::vector<SymbolValuePair>;

    std::vector<SymbolValuePairSet> allExpandedOptions;

    for (ShaderOption const& shaderOption : shaderOptions) {
        SymbolValuePairSet& expandedOptions = allExpandedOptions.emplace_back();
        for (std::string const& value : shaderOption.values) {
            expandedOptions.push_back({ shaderOption.symbol, value });
        }
    }

    // Combine the expanded options into permutations

    std::vector<SymbolValuePairSet> allPermutations = cartesianProduct(allExpandedOptions);
    ARKOSE_ASSERT(allPermutations.size() == numPermutations);

    compileSpec->permutations = std::move(allPermutations);

    return compileSpec;
}
