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

std::unique_ptr<HairAsset> HairAsset::createDebugStrand(u32 segmentCount, float length, float thickness)
{
    ARKOSE_ASSERT(segmentCount > 0);
    ARKOSE_ASSERT(length > 0.0f);
    ARKOSE_ASSERT(thickness > 0.0f);

    u32 const vertexCount = segmentCount + 1;

    auto hairAsset = std::make_unique<HairAsset>();
    hairAsset->setAssetFilePath("DebugSingleStrand");
    hairAsset->name = "DebugSingleStrand";

    hairAsset->strandCount = 1;
    hairAsset->defaultSegmentCount = segmentCount;
    hairAsset->defaultTransparency = 0.2f;

    hairAsset->positions.resize(vertexCount);
    hairAsset->colors.resize(vertexCount);
    hairAsset->thickness.resize(vertexCount);
    for (u32 vertexIdx = 0; vertexIdx < vertexCount; ++vertexIdx) {
        float const t = static_cast<float>(vertexIdx) / static_cast<float>(segmentCount);
        hairAsset->positions[vertexIdx] = vec3(0.0f, t * length, 0.0f);
        hairAsset->colors[vertexIdx] = (vertexIdx % 2u == 0u) ? vec3(1.0f, 0.1f, 0.1f) : vec3(0.1f, 0.1f, 1.0f);
        hairAsset->thickness[vertexIdx] = ark::lerp(thickness, 0.2f * thickness, t);
    }

    hairAsset->indices.reserve(vertexCount + 1);
    for (u32 vertexIdx = 0; vertexIdx < vertexCount; ++vertexIdx) {
        hairAsset->indices.push_back(vertexIdx);
    }
    hairAsset->indices.push_back(0xFFFFFFFFu);

    hairAsset->boundingBox.min = vec3(-thickness, 0.0f, -thickness);
    hairAsset->boundingBox.max = vec3(+thickness, length, +thickness);

    return hairAsset;
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
    if (segmentCounts.empty()) {
        return defaultSegmentCount;
    }
    return static_cast<u32>(segmentCounts[strandIdx]);
}

float HairAsset::thicknessForPoint(u32 pointIdx) const
{
    ARKOSE_ASSERT(pointIdx < positions.size());
    if (thickness.empty()) {
        return defaultThickness;
    }
    return thickness[pointIdx];
}

float HairAsset::transparencyForPoint(u32 pointIdx) const
{
    ARKOSE_ASSERT(pointIdx < positions.size());
    if (transparency.empty()) {
        return defaultTransparency;
    }
    return transparency[pointIdx];
}

vec3 HairAsset::colorForPoint(u32 pointIdx) const
{
    ARKOSE_ASSERT(pointIdx < positions.size());
    if (colors.empty()) {
        return defaultColor;
    }
    return colors[pointIdx];
}

std::span<u32 const> HairAsset::indicesForStrand(u32 strandIdx) const
{
    ARKOSE_ASSERT(strandIdx < strandCount);

    // Number of indices is number of segments +1, since segments are between points
    u32 count = segmentCountForStrand(strandIdx) + 1;

    // When calculating offset, consider both the +1 for points like above, but also the +1 for the primitive restart index
    u32 offset = 0;
    for (u32 idx = 0; idx < strandIdx; ++idx) {
        offset += segmentCountForStrand(idx) + 1 + 1;
    }

    ARKOSE_ASSERT(static_cast<size_t>(offset + count) <= indices.size());

    return { indices.data() + offset, count };
}
