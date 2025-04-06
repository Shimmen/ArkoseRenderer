#pragma once

#include "core/Assert.h"
#include "core/Logging.h"
#include "core/Types.h"
#include "utility/FileIO.h"
#include <cereal/cereal.hpp>
#include <cereal/archives/binary.hpp>
#include <cereal/archives/json.hpp>
#include <array>
#include <memory>
#include <string>
#include <string_view>

enum class AssetStorage {
    Binary,
    Json,
};

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

template<typename AssetType>
class Asset {
public:
    virtual ~Asset() { }

    // Name of asset, for inspecting and debugging purposes. If no name is specified it will be the filename without extension.
    std::string name {};

    // NOTE: I'd really like to make these non-pure virtual function and have a common implementation for them,
    // as they are already pretty much copies of each other. However, cereal, our serialization library doesn't
    // like that at all. I've tried a bunch of possible solutions but whatever I do it doesn't compile, or it
    // makes the code 10x more complex. For now, let's keep it simple with copy pasted code.
    virtual bool readFromFile(std::filesystem::path const& filePath) = 0;
    virtual bool writeToFile(std::filesystem::path const& filePath, AssetStorage assetStorage) const = 0;

    static bool isValidAssetPath(std::filesystem::path assetPath);

    virtual void setAssetFilePath(std::filesystem::path assetFilePath) { m_assetFilePath = std::move(assetFilePath); }
    virtual std::filesystem::path const& assetFilePath() const final { return m_assetFilePath; }

private:
    std::filesystem::path m_assetFilePath {};

};

template<typename AssetType>
bool Asset<AssetType>::isValidAssetPath(std::filesystem::path assetPath)
{
    return assetPath.extension() == AssetType::AssetFileExtension;
}

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
