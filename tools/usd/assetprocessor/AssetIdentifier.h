#pragma once

#include <core/Assert.h>
#include <string>
#include <fmt/format.h>

enum class AssetPlatform {
	Windows,
	MacOS,
};

class AssetIdentifier {
public:
	// NOTE: Relative to the src directory
	std::string sourcePath;
	AssetPlatform platform;
};

class ImageAssetIdentifier {
public:
    AssetIdentifier assetId;
    bool isNormalMap;
    bool sRGB;
};

inline std::string makeBuiltAssetPath(AssetIdentifier const& assetId)
{
    std::string_view sourcePath = assetId.sourcePath;
    if (sourcePath.length() > 0 && sourcePath[0] == '/') {
        sourcePath = sourcePath.substr(1);
    }

    std::string platformString;
    switch (assetId.platform) {
        case AssetPlatform::Windows:
            platformString = "Windows";
            break;
        case AssetPlatform::MacOS:
            platformString = "MacOS";
            break;
        default:
            ASSERT_NOT_REACHED();
            break;
    }

    // TODO: Include some kind of src file content hash?
    // Or perhaps better/faster is just a file last edit timestamp? Yes.
    std::string lastEditTimestamp = "18-07-2023H09M35S13ms9541"; // TODO: Make a ISO standard datetime string...

    return fmt::format("</{}>_{}_{}", sourcePath, platformString, lastEditTimestamp);
}
