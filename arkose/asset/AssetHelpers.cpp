#include "AssetHelpers.h"

#include "core/Assert.h"
#include "utility/FileIO.h"

bool AssetHelpers::isValidAssetPath(std::string_view assetPath, std::string_view extensionWithoutDot)
{
    // Need at least the extension length plus space for the dot before it
    const size_t minimumLength = extensionWithoutDot.length() + 1;

    if (assetPath.length() < minimumLength) {
        return false;
    }

    if (assetPath.at(assetPath.length() - minimumLength) != '.') {
        return false;
    }

    if (not assetPath.ends_with(extensionWithoutDot)) {
        return false;
    }

    return true;
}

std::unique_ptr<flatbuffers::Parser> AssetHelpers::createMaterialAssetParser()
{
    //constexpr const char* FlatbuffersDirectory = "arkose/asset/";
    //const char* IncludeDirectories[] = { FlatbuffersDirectory };

    //constexpr const char* MaterialAssetSchemaPath = "arkose/asset/MaterialAsset.fbs";
    constexpr const char* MaterialAssetSchemaPath = "MaterialAsset.fbs";
    std::string schema = FileIO::readEntireFile(MaterialAssetSchemaPath).value();

    auto parser = std::make_unique<flatbuffers::Parser>();
    bool success = parser->Parse(schema.data(), nullptr /*IncludeDirectories*/, MaterialAssetSchemaPath);

    ARKOSE_ASSERT(success);
    return parser;
}
