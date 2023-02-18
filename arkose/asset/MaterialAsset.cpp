#include "MaterialAsset.h"

#include "core/Assert.h"
#include "core/Logging.h"
#include "utility/FileIO.h"
#include "utility/Profiling.h"
#include <cereal/cereal.hpp>
#include <cereal/archives/binary.hpp>
#include <cereal/archives/json.hpp>
#include <mutex>

namespace {
    static std::mutex s_materialAssetCacheMutex {};
    static std::unordered_map<std::string, std::unique_ptr<MaterialAsset>> s_materialAssetCache {};
}

MaterialInput::MaterialInput() = default;
MaterialInput::~MaterialInput() = default;

MaterialAsset::MaterialAsset() = default;
MaterialAsset::~MaterialAsset() = default;

MaterialAsset* MaterialAsset::loadFromArkmat(std::string const& filePath)
{
    SCOPED_PROFILE_ZONE();

    if (not AssetHelpers::isValidAssetPath(filePath, MaterialAsset::AssetFileExtension)) {
        ARKOSE_LOG(Warning, "Trying to load material asset with invalid file extension: '{}'", filePath);
    }

    {
        SCOPED_PROFILE_ZONE_NAMED("Material cache - load");
        std::scoped_lock<std::mutex> lock { s_materialAssetCacheMutex };

        auto entry = s_materialAssetCache.find(filePath);
        if (entry != s_materialAssetCache.end()) {
            return entry->second.get();
        }
    }

    std::ifstream fileStream(filePath, std::ios::binary);
    if (not fileStream.is_open()) {
        return nullptr;
    }

    std::unique_ptr<MaterialAsset> newMaterialAsset {};

    cereal::BinaryInputArchive binaryArchive(fileStream);

    AssetHeader header;
    binaryArchive(header);

    if (header == AssetHeader(AssetMagicValue)) {

        newMaterialAsset = std::make_unique<MaterialAsset>();
        binaryArchive(*newMaterialAsset);

    } else {
    
        fileStream.seekg(0); // seek back to the start

        if (static_cast<char>(fileStream.peek()) != '{') {
            ARKOSE_LOG(Error, "Failed to parse json text for material asset '{}'", filePath);
            return nullptr;
        }

        cereal::JSONInputArchive jsonArchive(fileStream);

        newMaterialAsset = std::make_unique<MaterialAsset>();
        jsonArchive(*newMaterialAsset);

    }

    newMaterialAsset->m_assetFilePath = filePath;
    newMaterialAsset->name = FileIO::removeExtensionFromPath(FileIO::extractFileNameFromPath(filePath));

    {
        SCOPED_PROFILE_ZONE_NAMED("Material cache - store");
        std::scoped_lock<std::mutex> lock { s_materialAssetCacheMutex };
        s_materialAssetCache[filePath] = std::move(newMaterialAsset);
        return s_materialAssetCache[filePath].get();
    }
}

bool MaterialAsset::writeToArkmat(std::string_view filePath, AssetStorage assetStorage)
{
    SCOPED_PROFILE_ZONE();

    if (not AssetHelpers::isValidAssetPath(filePath, MaterialAsset::AssetFileExtension)) {
        ARKOSE_LOG(Error, "Trying to write material asset to file with invalid extension: '{}'", filePath);
        return false;
    }

    ARKOSE_ASSERT(m_assetFilePath.empty() || m_assetFilePath == filePath);
    m_assetFilePath = filePath;

    std::ofstream fileStream { m_assetFilePath, std::ios::binary | std::ios::trunc };
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
