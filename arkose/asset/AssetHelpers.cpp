#include "AssetHelpers.h"

#include "core/Assert.h"
#include "core/Logging.h"
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
