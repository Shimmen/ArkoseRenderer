#pragma once

#include "asset/Asset.h"
#include "asset/ImageAsset.h"
#include "asset/SerialisationHelpers.h"
#include "rendering/BlendMode.h"
#include "rendering/ImageFilter.h"
#include "rendering/ImageWrapMode.h"
#include "rendering/backend/base/Texture.h"
#include <optional>
#include <string>
#include <string_view>
#include <variant>

class MaterialInput {
public:
    MaterialInput();
    ~MaterialInput();

    bool hasPathToImage() const { return std::holds_alternative<std::string>(image); }
    void setPathToImage(std::string path) { image = std::move(path); }
    std::string_view pathToImage() const
    {
        ARKOSE_ASSERT(hasPathToImage());
        return std::get<std::string>(image);
    }

    // Path to an image or an image asset directly
    std::variant<std::string, std::weak_ptr<ImageAsset>> image;

    ImageWrapModes wrapModes { ImageWrapModes::repeatAll() };

    ImageFilter minFilter { ImageFilter::Linear };
    ImageFilter magFilter { ImageFilter::Linear };

    bool useMipmapping { true };
    ImageFilter mipFilter { ImageFilter::Linear };

    // Not serialized, can be used to store whatever intermediate you want
    int userData { -1 };

    template<class Archive>
    void serialize(Archive&);
};

class MaterialAsset final : public Asset<MaterialAsset> {
public:
    MaterialAsset();
    ~MaterialAsset();

    static constexpr const char* AssetFileExtension = "arkmat";
    static constexpr std::array<char, 4> AssetMagicValue = { 'a', 'm', 'a', 't' };

    // Load a material asset (cached) from an .arkmat file
    // TODO: Figure out how we want to return this! Basic type, e.g. MaterialAsset*, or something reference counted, e.g. shared_ptr or manual ref-count?
    static MaterialAsset* load(std::string const& filePath);

    virtual bool readFromFile(std::string_view filePath) override;
    virtual bool writeToFile(std::string_view filePath, AssetStorage assetStorage) override;

    template<class Archive>
    void serialize(Archive&);

    std::optional<MaterialInput> baseColor {};
    std::optional<MaterialInput> emissiveColor {};
    std::optional<MaterialInput> normalMap {};
    std::optional<MaterialInput> materialProperties {};

    vec4 colorTint { vec4(1.0f, 1.0f, 1.0f, 1.0f) };

    float metallicFactor { 0.0f };
    float roughnessFactor { 0.0f };

    BlendMode blendMode { BlendMode::Opaque };
    float maskCutoff { 1.0f };

    bool doubleSided { false };
};

////////////////////////////////////////////////////////////////////////////////
// Serialization

#include <cereal/cereal.hpp>
#include <cereal/types/memory.hpp>
#include <cereal/types/optional.hpp>
#include <cereal/types/variant.hpp>
#include <cereal/types/vector.hpp>

template<class Archive>
void MaterialInput::serialize(Archive& archive)
{
    archive(CEREAL_NVP(image));
    archive(CEREAL_NVP(wrapModes));
    archive(CEREAL_NVP(minFilter), CEREAL_NVP(magFilter));
    archive(CEREAL_NVP(useMipmapping), CEREAL_NVP(mipFilter));
}

template<class Archive>
void MaterialAsset::serialize(Archive& archive)
{
    archive(CEREAL_NVP(baseColor), CEREAL_NVP(emissiveColor), CEREAL_NVP(normalMap), CEREAL_NVP(materialProperties));
    archive(CEREAL_NVP(colorTint));
    archive(CEREAL_NVP(metallicFactor));
    archive(CEREAL_NVP(roughnessFactor));
    archive(CEREAL_NVP(blendMode), CEREAL_NVP(maskCutoff));
    archive(CEREAL_NVP(doubleSided));
}
