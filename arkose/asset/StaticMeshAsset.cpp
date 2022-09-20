#include "StaticMeshAsset.h"

#include "core/Assert.h"
#include "core/Logging.h"
#include "scene/loader/GltfLoader.h"
#include "utility/FileIO.h"
#include "utility/Profiling.h"
#include <mutex>

namespace {
    static std::mutex s_staticMeshAssetCacheMutex {};
    static std::unordered_map<std::string, std::unique_ptr<StaticMeshAsset>> s_staticMeshAssetCache {};
    static std::unique_ptr<flatbuffers::Parser> s_staticMeshAssetParser {};
}

StaticMeshAsset::StaticMeshAsset() = default;
StaticMeshAsset::~StaticMeshAsset() = default;

StaticMeshAsset::StaticMeshAsset(Arkose::Asset::StaticMeshAsset const* flatbuffersStaticMeshAsset, std::string filePath)
    : m_assetFilePath(std::move(filePath))
{
    ARKOSE_ASSERT(flatbuffersStaticMeshAsset != nullptr);
    flatbuffersStaticMeshAsset->UnPackTo(this);

    ARKOSE_ASSERT(m_assetFilePath.length() > 0);
}

StaticMeshAsset* StaticMeshAsset::loadFromArkmsh(std::string const& filePath)
{
    SCOPED_PROFILE_ZONE();

    if (not AssetHelpers::isValidAssetPath(filePath, Arkose::Asset::StaticMeshAssetExtension())) {
        ARKOSE_LOG(Warning, "Trying to load material asset with invalid file extension: '{}'", filePath);
    }

    {
        SCOPED_PROFILE_ZONE_NAMED("Static mesh cache - load");
        std::scoped_lock<std::mutex> lock { s_staticMeshAssetCacheMutex };

        auto entry = s_staticMeshAssetCache.find(filePath);
        if (entry != s_staticMeshAssetCache.end()) {
            return entry->second.get();
        }
    }

    auto maybeBinaryData = FileIO::readBinaryDataFromFile<uint8_t>(filePath);
    if (not maybeBinaryData.has_value()) {
        return nullptr;
    }

    void* binaryData = maybeBinaryData.value().data();
    auto const* flatbuffersStaticMeshAsset = Arkose::Asset::GetStaticMeshAsset(binaryData);

    if (!flatbuffersStaticMeshAsset) {
        return nullptr;
    }

    auto newMaterialAsset = std::unique_ptr<StaticMeshAsset>(new StaticMeshAsset(flatbuffersStaticMeshAsset, filePath));

    {
        SCOPED_PROFILE_ZONE_NAMED("Static mesh cache - store");
        std::scoped_lock<std::mutex> lock { s_staticMeshAssetCacheMutex };
        s_staticMeshAssetCache[filePath] = std::move(newMaterialAsset);
        return s_staticMeshAssetCache[filePath].get();
    }
}

bool StaticMeshAsset::writeToArkmsh(std::string_view filePath, AssetStorage assetStorage)
{
    SCOPED_PROFILE_ZONE();

    if (not AssetHelpers::isValidAssetPath(filePath, Arkose::Asset::StaticMeshAssetExtension())) {
        ARKOSE_LOG(Error, "Trying to write static mesh asset to file with invalid extension: '{}'", filePath);
        return false;
    }

    ARKOSE_ASSERT(m_assetFilePath.empty() || m_assetFilePath == filePath);
    m_assetFilePath = filePath;

    flatbuffers::FlatBufferBuilder builder {};
    auto asset = Arkose::Asset::StaticMeshAsset::Pack(builder, this);

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

        if (not s_staticMeshAssetParser) {
            s_staticMeshAssetParser = AssetHelpers::createAssetRuntimeParser("StaticMeshAsset.fbs");
        }

        std::string jsonText;
        if (not flatbuffers::GenerateText(*s_staticMeshAssetParser, data, &jsonText)) {
            ARKOSE_LOG(Error, "Failed to generate json text for static mesh asset");
            return false;
        }

        FileIO::writeTextDataToFile(std::string(filePath), jsonText);

    } break;
    }

    return true;
}
