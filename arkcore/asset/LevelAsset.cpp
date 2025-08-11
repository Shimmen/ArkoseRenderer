#include "LevelAsset.h"

#include "asset/AssetCache.h"
#include "asset/import/AssetImporter.h"
#include "core/Assert.h"
#include "core/Logging.h"
#include "utility/FileIO.h"
#include "utility/Profiling.h"

namespace {
AssetCache<LevelAsset> s_levelAssetCache {};
}

LevelAsset::LevelAsset() = default;
LevelAsset::~LevelAsset() = default;

LevelAsset* LevelAsset::load(std::filesystem::path const& filePath)
{
    SCOPED_PROFILE_ZONE();

    if (!isValidAssetPath(filePath)) {
        ARKOSE_LOG(Warning, "Trying to load level asset with invalid file extension: '{}'", filePath);
    }

    return s_levelAssetCache.getOrCreate(filePath, [&]() {
        auto newLevelAsset = std::make_unique<LevelAsset>();
        if (newLevelAsset->readFromFile(filePath)) {
            return newLevelAsset;
        } else {
            return std::unique_ptr<LevelAsset>();
        }
    });
}

std::unique_ptr<LevelAsset> LevelAsset::createFromAssetImportResult(ImportResult const& result)
{
    auto levelAsset = std::make_unique<LevelAsset>();

    // TODO: Also add lights, cameras, etc.

    for (MeshInstance const& meshInstance : result.meshInstances) {
        SceneObjectAsset sceneObject {};
        sceneObject.transform = meshInstance.transform;
        sceneObject.mesh = meshInstance.mesh->assetFilePath().string();
        levelAsset->objects.push_back(sceneObject);
    }

    for (ImportedCamera const& importedCamera : result.cameras) {
        CameraAsset& camera = levelAsset->cameras.emplace_back();
        camera.position = importedCamera.transform.positionInWorld();
        camera.orientation = importedCamera.transform.orientationInWorld();
        // TODO: Add zNear, zFar, and FOV to CameraAsset.
        // camera.zNear = importedCamera.zNear;
        // camera.zNear = importedCamera.zFar;
        // camera.verticalFieldOfView = importedCamera.verticalFieldOfView;
    }

    for (auto const& lightAssetSrc : result.lights) {
        // Simply copy over the light to the level asset
        levelAsset->lights.push_back(*lightAssetSrc.get());
    }

    return levelAsset;
}

bool LevelAsset::readFromFile(std::filesystem::path const& filePath)
{
    std::ifstream fileStream(filePath, std::ios::binary);
    if (not fileStream.is_open()) {
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

    this->setAssetFilePath(filePath);

    if (name.empty()) {
        name = filePath.stem().string();
    }

    return true;
}

bool LevelAsset::writeToFile(std::filesystem::path const& filePath, AssetStorage assetStorage) const
{
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
        archive(cereal::make_nvp("level", *this));
    } break;
    }

    fileStream.close();
    return true;
}
