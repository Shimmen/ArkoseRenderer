#include "StaticMeshAsset.h"

#include "core/Assert.h"
#include "core/Logging.h"
#include "physics/PhysicsMesh.h"
#include "utility/FileIO.h"
#include "utility/Profiling.h"
#include <cereal/archives/binary.hpp>
#include <cereal/archives/json.hpp>
#include <mutex>

namespace {
    static std::mutex s_staticMeshAssetCacheMutex {};
    static std::unordered_map<std::string, std::unique_ptr<StaticMeshAsset>> s_staticMeshAssetCache {};
}

StaticMeshSegmentAsset::StaticMeshSegmentAsset() = default;
StaticMeshSegmentAsset::~StaticMeshSegmentAsset() = default;

StaticMeshLODAsset::StaticMeshLODAsset() = default;
StaticMeshLODAsset::~StaticMeshLODAsset() = default;

StaticMeshAsset::StaticMeshAsset() = default;
StaticMeshAsset::~StaticMeshAsset() = default;

StaticMeshAsset* StaticMeshAsset::loadFromArkmsh(std::string const& filePath)
{
    SCOPED_PROFILE_ZONE();

    if (not AssetHelpers::isValidAssetPath(filePath, StaticMeshAsset::AssetFileExtension)) {
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

    std::ifstream fileStream(filePath, std::ios::binary);
    if (not fileStream.is_open()) {
        return nullptr;
    }

    std::unique_ptr<StaticMeshAsset> newStaticMeshAsset {};

    cereal::BinaryInputArchive binaryArchive(fileStream);

    AssetHeader header;
    binaryArchive(header);

    if (header == AssetHeader(AssetMagicValue)) {

        newStaticMeshAsset = std::make_unique<StaticMeshAsset>();
        binaryArchive(*newStaticMeshAsset);

    } else {

        fileStream.seekg(0); // seek back to the start

        if (static_cast<char>(fileStream.peek()) != '{') {
            ARKOSE_LOG(Error, "Failed to parse json text for static mesh asset '{}'", filePath);
            return nullptr;
        }

        cereal::JSONInputArchive jsonArchive(fileStream);

        newStaticMeshAsset = std::make_unique<StaticMeshAsset>();
        jsonArchive(*newStaticMeshAsset);
    }

    newStaticMeshAsset->m_assetFilePath = filePath;

    {
        SCOPED_PROFILE_ZONE_NAMED("Static mesh cache - store");
        std::scoped_lock<std::mutex> lock { s_staticMeshAssetCacheMutex };
        s_staticMeshAssetCache[filePath] = std::move(newStaticMeshAsset);
        return s_staticMeshAssetCache[filePath].get();
    }
}

bool StaticMeshAsset::writeToArkmsh(std::string_view filePath, AssetStorage assetStorage)
{
    SCOPED_PROFILE_ZONE();

    if (not AssetHelpers::isValidAssetPath(filePath, StaticMeshAsset::AssetFileExtension)) {
        ARKOSE_LOG(Error, "Trying to write static mesh asset to file with invalid extension: '{}'", filePath);
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
        archive(cereal::make_nvp("static_mesh", *this));
    } break;
    }

    fileStream.close();
    return true;
}

std::vector<PhysicsMesh> StaticMeshAsset::createPhysicsMeshes(size_t lodIdx) const
{
    ARKOSE_ASSERT(lodIdx < LODs.size());
    StaticMeshLODAsset const& lod = LODs[lodIdx];

    std::vector<PhysicsMesh> physicsMeshes {};
    for (StaticMeshSegmentAsset const& meshSegment : lod.meshSegments) {

        PhysicsMesh& physicsMesh = physicsMeshes.emplace_back();
        physicsMesh.positions = meshSegment.positions;
        physicsMesh.indices = meshSegment.indices;

        // TODO: Not yet implemented!
        // physicsMesh.material
    }

    return physicsMeshes;
}
