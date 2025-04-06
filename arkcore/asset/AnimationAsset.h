#pragma once

#include "core/Types.h"
#include "asset/Asset.h"
#include "utility/EnumHelpers.h"
#include <ark/vector.h>
#include <ark/matrix.h>
#include <vector>

enum class AnimationInterpolation {
    Linear,
    Step,
    CubicSpline,
};

constexpr std::array<const char*, 3> AnimationInterpolationNames = { "Linear", "Step", "CubicSpline" };
inline const char* AnimationInterpolationName(AnimationInterpolation interpolation)
{
    size_t idx = static_cast<size_t>(interpolation);
    return AnimationInterpolationNames[idx];
}

enum class AnimationTargetProperty {
    Translation,
    Rotation,
    Scale,
};

constexpr std::array<const char*, 3> AnimationTargetPropertyNames = { "Translation", "Rotation", "Scale" };
inline const char* AnimationTargetPropertyName(AnimationTargetProperty targetProperty)
{
    size_t idx = static_cast<size_t>(targetProperty);
    return AnimationTargetPropertyNames[idx];
}

template<typename PropertyType>
class AnimationSamplerAsset {
public:
    AnimationSamplerAsset() = default;
    ~AnimationSamplerAsset() = default;

    u32 inputTrackIdx {}; // Refers to an element in the parent animation asset array of `inputTracks`
    std::vector<PropertyType> outputValues {};
    AnimationInterpolation interpolation { AnimationInterpolation::Linear };
};

template<typename PropertyType>
class AnimationChannelAsset {
public:
    AnimationChannelAsset() = default;
    ~AnimationChannelAsset() = default;

    std::string targetReference; // name of target for binding
    AnimationTargetProperty targetProperty {};
    AnimationSamplerAsset<PropertyType> sampler {};
};

class AnimationAsset final : public Asset<AnimationAsset> {
public:
    AnimationAsset();
    ~AnimationAsset();

    static constexpr const char* AssetFileExtension = ".arkanim";
    static constexpr std::array<char, 4> AssetMagicValue = { 'a', 'a', 'n', 'm' };

    // Load an animation asset (cached) from an .arkanim file
    // TODO: Figure out how we want to return this! Basic type, e.g. AnimationAsset*, or something reference counted, e.g. shared_ptr or manual ref-count?
    static AnimationAsset* load(std::filesystem::path const& filePath);

    static AnimationAsset* manage(std::unique_ptr<AnimationAsset>&&);

    virtual bool readFromFile(std::filesystem::path const& filePath) override;
    virtual bool writeToFile(std::filesystem::path const& filePath, AssetStorage assetStorage) const override;

    template<class Archive>
    void serialize(Archive&, u32 version);

    // List of time/input tracks for sampling
    std::vector<std::vector<float>> inputTracks {};

    std::vector<AnimationChannelAsset<f32>> floatPropertyChannels {};
    std::vector<AnimationChannelAsset<vec2>> float2PropertyChannels {};
    std::vector<AnimationChannelAsset<vec3>> float3PropertyChannels {};
    std::vector<AnimationChannelAsset<vec4>> float4PropertyChannels {};
};

////////////////////////////////////////////////////////////////////////////////
// Serialization

#include <cereal/cereal.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/vector.hpp>
#include "asset/SerialisationHelpers.h"

enum class AnimationAssetVersion : u32 {
    Initial = 0,
    ////////////////////////////////////////////////////////////////////////////
    // Add new versions above this delimiter
    LatestVersion
};

CEREAL_CLASS_VERSION(AnimationAsset, toUnderlying(AnimationAssetVersion::LatestVersion))

template<class Archive>
std::string save_minimal(Archive const&, AnimationTargetProperty const& targetProperty)
{
    return AnimationTargetPropertyName(targetProperty);
}

template<class Archive>
void load_minimal(Archive const&, AnimationTargetProperty& targetProperty, std::string const& value)
{
    if (value == AnimationTargetPropertyName(AnimationTargetProperty::Translation)) {
        targetProperty = AnimationTargetProperty::Translation;
    } else if (value == AnimationTargetPropertyName(AnimationTargetProperty::Rotation)) {
        targetProperty = AnimationTargetProperty::Rotation;
    } else if (value == AnimationTargetPropertyName(AnimationTargetProperty::Scale)) {
        targetProperty = AnimationTargetProperty::Scale;
    } else {
        ASSERT_NOT_REACHED();
    }
}

template<class Archive>
std::string save_minimal(Archive const&, AnimationInterpolation const& interpolation)
{
    return AnimationInterpolationName(interpolation);
}

template<class Archive>
void load_minimal(Archive const&, AnimationInterpolation& interpolation, std::string const& value)
{
    if (value == AnimationInterpolationName(AnimationInterpolation::Linear)) {
        interpolation = AnimationInterpolation::Linear;
    } else if (value == AnimationInterpolationName(AnimationInterpolation::Step)) {
        interpolation = AnimationInterpolation::Step;
    } else if (value == AnimationInterpolationName(AnimationInterpolation::CubicSpline)) {
        interpolation = AnimationInterpolation::CubicSpline;
    } else {
        ASSERT_NOT_REACHED();
    }
}

template<class Archive, typename PropertyType>
void serialize(Archive& archive, AnimationSamplerAsset<PropertyType>& samplerAsset, u32 version)
{
    //archive(cereal::make_nvp("times", samplerAsset.times));
    archive(cereal::make_nvp("inputTrackIdx", samplerAsset.inputTrackIdx));
    archive(cereal::make_nvp("outputValues", samplerAsset.outputValues));
    archive(cereal::make_nvp("interpolation", samplerAsset.interpolation));
}

template<class Archive, typename PropertyType>
void serialize(Archive& archive, AnimationChannelAsset<PropertyType>& channelAsset, u32 version)
{
    archive(cereal::make_nvp("targetReference", channelAsset.targetReference));
    archive(cereal::make_nvp("targetProperty", channelAsset.targetProperty));
    archive(cereal::make_nvp("sampler", channelAsset.sampler));
}

template<class Archive>
void AnimationAsset::serialize(Archive& archive, u32 versionInt)
{
    // NOTE: Example of how versioning works. Keep around here for when we need it!
    //auto version = static_cast<AnimationAssetVersion>(versionInt);
    //if (version >= AnimationAssetVersion::SomethingWasAdded) { serializeSomething(); }

    archive(cereal::make_nvp("name", name));
    archive(cereal::make_nvp("inputTracks", inputTracks));
    archive(cereal::make_nvp("floatPropertyChannels", floatPropertyChannels));
    archive(cereal::make_nvp("float2PropertyChannels", float2PropertyChannels));
    archive(cereal::make_nvp("float3PropertyChannels", float3PropertyChannels));
    archive(cereal::make_nvp("float4PropertyChannels", float4PropertyChannels));
}
