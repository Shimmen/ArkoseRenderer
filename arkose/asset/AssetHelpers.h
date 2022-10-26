#pragma once

#include <cereal/cereal.hpp>
#include <string_view>
#include <flatbuffers/idl.h>

enum class AssetStorage {
    Binary,
    Json,
};

namespace AssetHelpers {

bool isValidAssetPath(std::string_view assetPath, std::string_view extensionWithoutDot);

std::unique_ptr<flatbuffers::Parser> createAssetRuntimeParser(std::string_view schemaFilename);

}

struct AssetHeader {
    char magicValue[4];

    bool operator==(AssetHeader const& other) const
    {
        for (int i = 0; i < 4; ++i) {
            if (magicValue[i] != other.magicValue[i]) {
                return false;
            }
        }
        return true;
    }
};

template<class Archive>
void serialize(Archive& archive, AssetHeader& header)
{
    archive(header.magicValue);
}
