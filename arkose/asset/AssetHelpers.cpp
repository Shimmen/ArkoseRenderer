#include "AssetHelpers.h"

#include "core/Assert.h"
#include "core/Logging.h"
#include "utility/FileIO.h"
#include <fmt/format.h>

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

std::unique_ptr<flatbuffers::Parser> AssetHelpers::createAssetRuntimeParser(std::string_view schemaFilename)
{
    // TODO: Set this from CMake depending on where we copy files to during pre-build!
    constexpr const char* RuntimeSchemaDirectory = "schema";

    // Relative to the schema asset we're loading the include directory is the current one
    constexpr const char* schemaDirRelativeToSource = "";
    const char* includePaths[] = { schemaDirRelativeToSource, nullptr };

    std::string schemaFilePath = fmt::format("{}/{}", RuntimeSchemaDirectory, schemaFilename);
    auto maybeSchema = FileIO::readEntireFile(schemaFilePath);
    if (not maybeSchema.has_value()) {
        ARKOSE_LOG(Error, "Failed to read flatbuffers schema file '{}' at path '{}'", schemaFilename, schemaFilePath);
        return nullptr;
    }

    std::string const& schemaString = maybeSchema.value();

    auto parser = std::make_unique<flatbuffers::Parser>();
    bool parseSuccess = parser->Parse(schemaString.data(), includePaths, schemaFilePath.data());

    if (not parseSuccess) {
        ARKOSE_LOG(Error, "Error trying to parse flatbuffers schema:\n\t{}", parser->error_);
        return nullptr;
    }

    return parser;
}
