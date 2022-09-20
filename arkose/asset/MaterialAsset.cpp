#include "MaterialAsset.h"

#include "core/Assert.h"
#include "core/Logging.h"
#include "utility/FileIO.h"
#include "utility/Profiling.h"
#include <mutex>

namespace {
    static std::mutex s_materialAssetCacheMutex {};
    static std::unordered_map<std::string, std::unique_ptr<MaterialAsset>> s_materialAssetCache {};
    static std::unique_ptr<flatbuffers::Parser> s_materialAssetParser {};
}

MaterialAsset::MaterialAsset() = default;
MaterialAsset::~MaterialAsset() = default;

MaterialAsset::MaterialAsset(Arkose::Asset::MaterialAsset const* flatbuffersMaterialAsset, std::string filePath)
    : m_assetFilePath(std::move(filePath))
{
    ARKOSE_ASSERT(flatbuffersMaterialAsset != nullptr);
    flatbuffersMaterialAsset->UnPackTo(this);

    ARKOSE_ASSERT(m_assetFilePath.length() > 0);
}

MaterialAsset* MaterialAsset::loadFromArkmat(std::string const& filePath)
{
    SCOPED_PROFILE_ZONE();

    if (not AssetHelpers::isValidAssetPath(filePath, Arkose::Asset::MaterialAssetExtension())) {
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

    auto maybeBinaryData = FileIO::readBinaryDataFromFile<uint8_t>(filePath);
    if (not maybeBinaryData.has_value()) {
        return nullptr;
    }

    void* binaryData = maybeBinaryData.value().data();
    auto const* flatbuffersMaterialAsset = Arkose::Asset::GetMaterialAsset(binaryData);

    if (!flatbuffersMaterialAsset) {
        return nullptr;
    }

    auto newMaterialAsset = std::unique_ptr<MaterialAsset>(new MaterialAsset(flatbuffersMaterialAsset, filePath));

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

    if (not AssetHelpers::isValidAssetPath(filePath, Arkose::Asset::MaterialAssetExtension())) {
        ARKOSE_LOG(Error, "Trying to write material asset to file with invalid extension: '{}'", filePath);
        return false;
    }

    ARKOSE_ASSERT(m_assetFilePath.empty() || m_assetFilePath == filePath);
    m_assetFilePath = filePath;

    flatbuffers::FlatBufferBuilder builder {};
    auto asset = Arkose::Asset::MaterialAsset::Pack(builder, this);

    if (asset.IsNull()) {
        return false;
    }

    builder.Finish(asset);

    uint8_t* data = builder.GetBufferPointer();
    size_t size = static_cast<size_t>(builder.GetSize());

    switch (assetStorage) {
    case AssetStorage::Binary: 
        FileIO::writeBinaryDataToFile(std::string(filePath), data, size);
        break;
    case AssetStorage::Json: {

        if (not s_materialAssetParser) {
            s_materialAssetParser = AssetHelpers::createAssetRuntimeParser("MaterialAsset.fbs");
        }

        std::string jsonText;
        if (not flatbuffers::GenerateText(*s_materialAssetParser, data, &jsonText)) {
            ARKOSE_LOG(Error, "Failed to generate json text for material asset");
            return false;
        }

        FileIO::writeTextDataToFile(std::string(filePath), jsonText);

    } break;
    }

    return true;
}
