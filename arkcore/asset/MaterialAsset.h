#pragma once

#include "asset/Asset.h"
#include "asset/ImageAsset.h"
#include "asset/SerialisationHelpers.h"
#include "rendering/BlendMode.h"
#include "rendering/Brdf.h"
#include "rendering/ImageFilter.h"
#include "rendering/ImageWrapMode.h"
#include <optional>
#include <string>
#include <string_view>

class MaterialInput {
public:
    explicit MaterialInput(std::string_view imagePath)
        : image(imagePath)
    {
    }

    MaterialInput();
    ~MaterialInput();

    // TODO: Change to use std::filesystem::path
    std::string image;

    ImageWrapModes wrapModes { ImageWrapModes::repeatAll() };

    ImageFilter minFilter { ImageFilter::Linear };
    ImageFilter magFilter { ImageFilter::Linear };

    bool useMipmapping { true };
    ImageFilter mipFilter { ImageFilter::Linear };

    // Not serialized, can be used to store whatever intermediate you want
    int userData { -1 };

    bool operator==(MaterialInput const& rhs) const
    {
        return image == rhs.image
            && wrapModes == rhs.wrapModes
            && minFilter == rhs.minFilter
            && magFilter == rhs.magFilter
            && useMipmapping == rhs.useMipmapping
            && mipFilter == rhs.mipFilter;
    }

    template<class Archive>
    void serialize(Archive&, u32 version);
};

namespace std {

    template<>
    struct hash<MaterialInput> {
        std::size_t operator()(const MaterialInput& input) const
        {
            auto pathHash = std::hash<std::string_view>()(input.image);
            auto wrapModesHash = std::hash<ImageWrapModes>()(input.wrapModes);

            auto filterHash = hashCombine(std::hash<ImageFilter>()(input.minFilter),
                                          std::hash<ImageFilter>()(input.magFilter));
            auto mipHash = hashCombine(std::hash<bool>()(input.useMipmapping),
                                       std::hash<ImageFilter>()(input.mipFilter));

            return hashCombine(
                hashCombine(pathHash, wrapModesHash),
                hashCombine(filterHash, mipHash));
        }
    };

}

class MaterialAsset final : public Asset<MaterialAsset> {
public:
    MaterialAsset();
    ~MaterialAsset();

    static constexpr const char* AssetFileExtension = ".arkmat";
    static constexpr std::array<char, 4> AssetMagicValue = { 'a', 'm', 'a', 't' };

    // Load a material asset (cached) from an .arkmat file
    // TODO: Figure out how we want to return this! Basic type, e.g. MaterialAsset*, or something reference counted, e.g. shared_ptr or manual ref-count?
    static MaterialAsset* load(std::filesystem::path const& filePath);

    static MaterialAsset* manage(std::unique_ptr<MaterialAsset>&&);

    virtual bool readFromFile(std::filesystem::path const& filePath) override;
    virtual bool writeToFile(std::filesystem::path const& filePath, AssetStorage assetStorage) const override;

    template<class Archive>
    void serialize(Archive&, u32 version);

    Brdf brdf { Brdf::Default };

    std::optional<MaterialInput> baseColor {};
    std::optional<MaterialInput> emissiveColor {};
    std::optional<MaterialInput> normalMap {};
    std::optional<MaterialInput> bentNormalMap {};
    std::optional<MaterialInput> materialProperties {};
    std::optional<MaterialInput> occlusionMap {}; // TODO: Probably pack into `materialProperties`

    vec4 colorTint { vec4(1.0f, 1.0f, 1.0f, 1.0f) };

    float metallicFactor { 0.0f };
    float roughnessFactor { 0.0f };
    vec3 emissiveFactor { 0.0f, 0.0f, 0.0f };

    float clearcoat { 0.0f };
    float clearcoatRoughness { 0.0f };

    float indexOfRefraction { 1.5f };
    float calculateDielectricReflectance(float interfaceIOR = 1.0f) const;

    BlendMode blendMode { BlendMode::Opaque };
    float maskCutoff { 1.0f };

    bool doubleSided { false };
};

////////////////////////////////////////////////////////////////////////////////
// Serialization

#include "utility/EnumHelpers.h"
#include <cereal/cereal.hpp>
#include <cereal/types/memory.hpp>
#include <cereal/types/optional.hpp>
#include <cereal/types/variant.hpp>
#include <cereal/types/vector.hpp>

enum class MaterialAssetVersion : u32 {
    Initial = 0,
    AddEmissiveFactor,
    AddBentNormalMap,
    AddOcclusionMap,
    AddClearcoat,
    AddIndexOfRefraction,
    ////////////////////////////////////////////////////////////////////////////
    // Add new versions above this delimiter
    VersionCount,
    LatestVersion = VersionCount - 1
};

CEREAL_CLASS_VERSION(MaterialInput, toUnderlying(MaterialAssetVersion::LatestVersion))
CEREAL_CLASS_VERSION(MaterialAsset, toUnderlying(MaterialAssetVersion::LatestVersion))

template<class Archive>
void MaterialInput::serialize(Archive& archive, u32 version)
{
    archive(CEREAL_NVP(image));
    archive(CEREAL_NVP(wrapModes));
    archive(CEREAL_NVP(minFilter), CEREAL_NVP(magFilter));
    archive(CEREAL_NVP(useMipmapping), CEREAL_NVP(mipFilter));
}

template<class Archive>
void MaterialAsset::serialize(Archive& archive, u32 version)
{
    archive(CEREAL_NVP(brdf));
    archive(CEREAL_NVP(baseColor));
    archive(CEREAL_NVP(emissiveColor));
    archive(CEREAL_NVP(normalMap));
    if (version >= toUnderlying(MaterialAssetVersion::AddBentNormalMap)) {
        archive(CEREAL_NVP(bentNormalMap));
    } else {
        bentNormalMap = {};
    }
    archive(CEREAL_NVP(materialProperties));
    if (version >= toUnderlying(MaterialAssetVersion::AddOcclusionMap)) {
        archive(CEREAL_NVP(occlusionMap));
    } else {
        occlusionMap = {};
    }
    archive(CEREAL_NVP(colorTint));
    archive(CEREAL_NVP(metallicFactor));
    archive(CEREAL_NVP(roughnessFactor));
    if (version >= toUnderlying(MaterialAssetVersion::AddEmissiveFactor)) {
        archive(CEREAL_NVP(emissiveFactor));
    } else {
        emissiveFactor = vec3(0.0f);
    }
    if (version >= toUnderlying(MaterialAssetVersion::AddClearcoat)) {
        archive(CEREAL_NVP(clearcoat));
        archive(CEREAL_NVP(clearcoatRoughness));
    } else {
        clearcoat = 0.0f;
        clearcoatRoughness = 0.0f;
    }
    if (version >= toUnderlying(MaterialAssetVersion::AddIndexOfRefraction)) {
        archive(CEREAL_NVP(indexOfRefraction));
    } else {
        indexOfRefraction = 1.5f;
    }
    archive(CEREAL_NVP(blendMode), CEREAL_NVP(maskCutoff));
    archive(CEREAL_NVP(doubleSided));
}
