#include "HairAsset.h"

#include "asset/AssetCache.h"
#include "core/Assert.h"
#include "core/Logging.h"
#include "utility/Profiling.h"

#include <cereal/archives/binary.hpp>
#include <cereal/archives/json.hpp>
#include <fstream>

namespace {
AssetCache<HairAsset> s_hairAssetCache {};
}

HairAsset::HairAsset() = default;
HairAsset::~HairAsset() = default;

HairAsset* HairAsset::load(std::filesystem::path const& filePath)
{
    SCOPED_PROFILE_ZONE();

    if (!isValidAssetPath(filePath)) {
        ARKOSE_LOG(Warning, "Trying to load hair asset with invalid file extension: '{}'", filePath);
    }

    return s_hairAssetCache.getOrCreate(filePath, [&]() {
        auto newHairAsset = std::make_unique<HairAsset>();
        if (newHairAsset->readFromFile(filePath)) {
            return newHairAsset;
        }
        return std::unique_ptr<HairAsset>();
    });
}

HairAsset* HairAsset::manage(std::unique_ptr<HairAsset>&& hairAsset)
{
    ARKOSE_ASSERT(!hairAsset->assetFilePath().empty());
    return s_hairAssetCache.put(hairAsset->assetFilePath(), std::move(hairAsset));
}

bool HairAsset::readFromFile(std::filesystem::path const& filePath)
{
    std::ifstream fileStream(filePath, std::ios::binary);
    if (!fileStream.is_open()) {
        ARKOSE_LOG(Error, "Failed to load hair asset at path '{}'", filePath);
        return false;
    }

    cereal::BinaryInputArchive binaryArchive(fileStream);

    AssetHeader header;
    binaryArchive(header);

    if (header == AssetHeader(AssetMagicValue)) {

        binaryArchive(*this);

    } else {

        fileStream.seekg(0);

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

bool HairAsset::writeToFile(std::filesystem::path const& filePath, AssetStorage assetStorage) const
{
    SCOPED_PROFILE_ZONE();

    if (!isValidAssetPath(filePath)) {
        ARKOSE_LOG(Error, "Trying to write asset to file with invalid extension: '{}'", filePath);
        return false;
    }

    std::ofstream fileStream { filePath, std::ios::binary | std::ios::trunc };
    if (!fileStream.is_open()) {
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
        archive(cereal::make_nvp("hair", *this));
    } break;
    }

    fileStream.close();
    return true;
}

u32 HairAsset::segmentCountForStrand(u32 strandIdx) const
{
    ARKOSE_ASSERT(strandIdx < strandCount);
    if (segments.empty()) {
        return defaultSegmentCount;
    }
    return static_cast<u32>(segments[strandIdx]);
}

float HairAsset::thicknessForPoint(u32 pointIdx) const
{
    ARKOSE_ASSERT(pointIdx < pointCount);
    if (thickness.empty()) {
        return defaultThickness;
    }
    return thickness[pointIdx];
}

float HairAsset::transparencyForPoint(u32 pointIdx) const
{
    ARKOSE_ASSERT(pointIdx < pointCount);
    if (transparency.empty()) {
        return defaultTransparency;
    }
    return transparency[pointIdx];
}

vec3 HairAsset::colorForPoint(u32 pointIdx) const
{
    ARKOSE_ASSERT(pointIdx < pointCount);
    if (colors.empty()) {
        return defaultColor;
    }
    return colors[pointIdx];
}
