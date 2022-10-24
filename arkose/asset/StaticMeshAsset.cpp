#include "StaticMeshAsset.h"

#include "core/Assert.h"
#include "core/Logging.h"
#include "physics/PhysicsMesh.h"
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

    bool isValidBinaryBuffer = Arkose::Asset::StaticMeshAssetBufferHasIdentifier(binaryData);
    if (not isValidBinaryBuffer) {

        // See if we can parse it as json

        // First check: do we at least start with a '{' character?
        if (maybeBinaryData.value().size() == 0 || *reinterpret_cast<const char*>(binaryData) != '{') {
            return nullptr;
        }

        std::string asciiData = FileIO::readEntireFile(filePath).value();

        if (not s_staticMeshAssetParser) {
            s_staticMeshAssetParser = AssetHelpers::createAssetRuntimeParser("StaticMeshAsset.fbs");
        }

        if (not s_staticMeshAssetParser->ParseJson(asciiData.c_str(), filePath.c_str())) {
            ARKOSE_LOG(Error, "Failed to parse json text for static mesh asset:\n\t{}", s_staticMeshAssetParser->error_);
            return nullptr;
        }

        // Use the now filled-in builder's buffer as the binary data input
        binaryData = s_staticMeshAssetParser->builder_.GetBufferPointer();
    }

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

    builder.Finish(asset, Arkose::Asset::StaticMeshAssetIdentifier());

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

std::vector<PhysicsMesh> StaticMeshAsset::createPhysicsMeshes(size_t lodIdx) const
{
    ARKOSE_ASSERT(lodIdx < lods.size());
    auto const& lod = lods[lodIdx];

    std::vector<PhysicsMesh> physicsMeshes {};
    for (auto const& meshSegment : lod->mesh_segments) {

        PhysicsMesh& physicsMesh = physicsMeshes.emplace_back();

        physicsMesh.positions.reserve(meshSegment->positions.size());
        for (Arkose::Asset::Vec3 pos : meshSegment->positions) {
            physicsMesh.positions.emplace_back(pos.x(), pos.y(), pos.z());
        }

        physicsMesh.indices = meshSegment->indices;

        // TODO: Not yet implemented!
        // physicsMesh.material
    }

    return physicsMeshes;
}
