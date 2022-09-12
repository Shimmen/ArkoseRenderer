#pragma once

#include <string_view>
#include <flatbuffers/idl.h>

enum class AssetStorage {
    Binary,
    Json,
};

namespace AssetHelpers {

bool isValidAssetPath(std::string_view assetPath, std::string_view extensionWithoutDot);

std::unique_ptr<flatbuffers::Parser> createMaterialAssetParser();

}
