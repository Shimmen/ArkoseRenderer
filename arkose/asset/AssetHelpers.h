#pragma once

#include "core/Types.h"
#include <array>
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
    std::array<char, 4> magicValue {};

    constexpr AssetHeader() = default;
    constexpr AssetHeader(std::array<char, 4> value)
        : magicValue(value)
    {
    }

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

////////////////////////////////////////////////////////////////////////////////
// Serialization

#include <cereal/cereal.hpp>
#include <cereal/types/array.hpp>

template<class Archive>
void save(Archive& archive, AssetHeader const& header)
{
    u32 value = 0u;
    value |= static_cast<u32>(header.magicValue[0]) << 0;
    value |= static_cast<u32>(header.magicValue[1]) << 8;
    value |= static_cast<u32>(header.magicValue[2]) << 16;
    value |= static_cast<u32>(header.magicValue[3]) << 24;

    archive(value);
}

template<class Archive>
void load(Archive& archive, AssetHeader& header)
{
    u32 value;
    archive(value);

    header.magicValue[0] = (value >> 0) & 0xFF;
    header.magicValue[1] = (value >> 8) & 0xFF;
    header.magicValue[2] = (value >> 16) & 0xFF;
    header.magicValue[3] = (value >> 24) & 0xFF;
}
