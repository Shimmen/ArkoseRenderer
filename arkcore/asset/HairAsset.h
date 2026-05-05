#pragma once

#include "asset/Asset.h"
#include "asset/SerialisationHelpers.h"
#include "core/Types.h"
#include <ark/aabb.h>
#include <array>
#include <memory>
#include <vector>

class HairAsset final : public Asset<HairAsset> {
public:
    HairAsset();
    ~HairAsset();

    static constexpr const char* AssetFileExtension = ".arkhair";
    static constexpr std::array<char, 4> AssetMagicValue = { 'a', 'h', 'a', 'i' };

    static HairAsset* load(std::filesystem::path const& filePath);
    static HairAsset* manage(std::unique_ptr<HairAsset>&& hairAsset);

    bool readFromFile(std::filesystem::path const& filePath) override;
    bool writeToFile(std::filesystem::path const& filePath, AssetStorage assetStorage) const override;

    template<class Archive>
    void serialize(Archive&, u32 version);

    u32 strandCount {};
    u32 pointCount {};

    u32 defaultSegmentCount { 0 };
    float defaultThickness { 1.0f };
    float defaultTransparency { 0.0f };
    vec3 defaultColor { 1.0f, 1.0f, 1.0f };

    // For empty arrays, assume defaults for all N points/strands
    std::vector<u16> segments {};
    std::vector<vec3> points {};
    std::vector<float> thickness {};
    std::vector<float> transparency {};
    std::vector<vec3> colors {};

    ark::aabb3 boundingBox {};

    u32 segmentCountForStrand(u32 strandIdx) const;
    float thicknessForPoint(u32 pointIdx) const;
    float transparencyForPoint(u32 pointIdx) const;
    vec3 colorForPoint(u32 pointIdx) const;
};

////////////////////////////////////////////////////////////////////////////////
// Serialization

#include "utility/EnumHelpers.h"
#include <cereal/cereal.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/vector.hpp>

enum class HairAssetVersion : u32 {
    Initial = 0,
    ////////////////////////////////////////////////////////////////////////////
    // Add new versions above this delimiter
    VersionCount,
    LatestVersion = VersionCount - 1
};

CEREAL_CLASS_VERSION(HairAsset, toUnderlying(HairAssetVersion::LatestVersion))

template<class Archive>
void HairAsset::serialize(Archive& archive, u32 version)
{
    archive(CEREAL_NVP(name));

    archive(CEREAL_NVP(strandCount));
    archive(CEREAL_NVP(pointCount));

    archive(CEREAL_NVP(defaultSegmentCount));
    archive(CEREAL_NVP(defaultThickness));
    archive(CEREAL_NVP(defaultTransparency));
    archive(CEREAL_NVP(defaultColor));

    archive(CEREAL_NVP(segments));
    archive(CEREAL_NVP(points));
    archive(CEREAL_NVP(thickness));
    archive(CEREAL_NVP(transparency));
    archive(CEREAL_NVP(colors));

    archive(CEREAL_NVP(boundingBox));
}
