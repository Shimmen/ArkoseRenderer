#include "MaterialAsset.h"

#include "asset/AssetCache.h"
#include "core/Assert.h"
#include "core/Logging.h"
#include "utility/FileIO.h"
#include "utility/Profiling.h"
#include <cereal/cereal.hpp>
#include <cereal/archives/binary.hpp>
#include <cereal/archives/json.hpp>

namespace {
AssetCache<MaterialAsset> s_materialAssetCache {};
}

MaterialInput::MaterialInput() = default;
MaterialInput::~MaterialInput() = default;

MaterialAsset::MaterialAsset() = default;
MaterialAsset::~MaterialAsset() = default;

MaterialAsset* MaterialAsset::load(std::filesystem::path const& filePath)
{
    SCOPED_PROFILE_ZONE();

    if (!isValidAssetPath(filePath)) {
        ARKOSE_LOG(Warning, "Trying to load material asset with invalid file extension: '{}'", filePath);
    }

    return s_materialAssetCache.getOrCreate(filePath, [&]() {
        auto newMaterialAsset = std::make_unique<MaterialAsset>();
        if (newMaterialAsset->readFromFile(filePath)) {
            return newMaterialAsset;
        } else {
            return std::unique_ptr<MaterialAsset>();
        }
    });
}

MaterialAsset* MaterialAsset::manage(std::unique_ptr<MaterialAsset>&& materialAsset)
{
    ARKOSE_ASSERT(!materialAsset->assetFilePath().empty());
    return s_materialAssetCache.put(materialAsset->assetFilePath(), std::move(materialAsset));
}

bool MaterialAsset::readFromFile(std::filesystem::path const& filePath)
{
    std::ifstream fileStream(filePath, std::ios::binary);
    if (!fileStream.is_open()) {
        ARKOSE_LOG(Error, "Failed to load material asset at path '{}'", filePath);
        return false;
    }

    cereal::BinaryInputArchive binaryArchive(fileStream);

    AssetHeader header;
    binaryArchive(header);

    if (header == AssetHeader(AssetMagicValue)) {

        binaryArchive(*this);

    } else {

        fileStream.seekg(0); // seek back to the start

        if (static_cast<char>(fileStream.peek()) != '{') {
            ARKOSE_LOG(Error, "Failed to parse json text for asset '{}'", filePath);
            return false;
        }

        cereal::JSONInputArchive jsonArchive(fileStream);
        jsonArchive(*this);
    }

    setAssetFilePath(filePath);

    if (name.empty()) {
        name = filePath.stem().string();
    }

    return true;
}

bool MaterialAsset::writeToFile(std::filesystem::path const& filePath, AssetStorage assetStorage) const
{
    SCOPED_PROFILE_ZONE();

    if (not isValidAssetPath(filePath)) {
        ARKOSE_LOG(Error, "Trying to write asset to file with invalid extension: '{}'", filePath);
        return false;
    }

    std::ofstream fileStream { filePath, std::ios::binary | std::ios::trunc };
    if (not fileStream.is_open()) {
        return false;
    }

    switch (assetStorage) {
    case AssetStorage::Binary: {
        cereal::BinaryOutputArchive archive(fileStream);
        archive(AssetHeader(AssetMagicValue));
        archive(*this);
    } break;
    case AssetStorage::Json: {
        cereal::JSONOutputArchive archive(fileStream);
        archive(cereal::make_nvp("material", *this));
    } break;
    }

    fileStream.close();
    return true;
}

float MaterialAsset::calculateDielectricReflectance(float interfaceIOR) const
{
    const float n1 = indexOfRefraction;
    const float n2 = interfaceIOR;

    return ark::square((n1 - n2) / (n1 + n2));
}
