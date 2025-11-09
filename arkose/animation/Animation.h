#pragma once

#include "asset/AnimationAsset.h"
#include <memory>
#include <string>
#include <unordered_map>


class AnimationAsset;
class Transform;
struct SkeletalMeshInstance;
struct StaticMeshInstance;

//using AnimationBindMap = std::unordered_map<std::string, StaticMeshInstance*>;

class Animation {
public:
    Animation(AnimationAsset const*);
    ~Animation();

    // static std::unique_ptr<Animation> bind(AnimationAsset*, AnimationBindMap&&);
    static std::unique_ptr<Animation> bind(AnimationAsset*, SkeletalMeshInstance&);

    void setSkeletalMeshInstance(SkeletalMeshInstance&);

    void tick(float deltaTime);
    void reset();

    enum class PlaybackMode {
        OneShot,
        Looping,
    };

    PlaybackMode playbackMode() const { return m_playbackMode; }
    void setPlaybackMode(PlaybackMode playbackMode) { m_playbackMode = playbackMode; }

private:
    struct SampledInputTrack {
        i32 idx0 { -1 };
        i32 idx1 { -1 };
        float interpolation { 0.0f };
    };

    SampledInputTrack evaluateInputTrack(size_t inputTrackIdx, std::vector<float> const& inputTrack);

    template<typename PropertyType>
    PropertyType evaluateAnimationChannel(SampledInputTrack const&, AnimationChannelAsset<PropertyType> const&, size_t offset = 0, size_t stride = 1);

    Transform* findTransformForTarget(std::string const& targetReference);

private:
    // Source asset and owner of all actual animation data (for now at least)
    AnimationAsset const* m_asset { nullptr };

    // The current animation time
    float m_animationTime { 0.0f };

    // Should this animation loop or not, and more such modes
    PlaybackMode m_playbackMode { PlaybackMode::OneShot };

    // The skeletal mesh instance that this animation will animate
    SkeletalMeshInstance* m_skeletalMeshInstance { nullptr };
};

template<typename PropertyType>
PropertyType Animation::evaluateAnimationChannel(SampledInputTrack const& sampledInput,
                                                 AnimationChannelAsset<PropertyType> const& channel,
                                                 size_t offset, size_t stride)
{
    if (sampledInput.idx1 == -1) {
        ARKOSE_ASSERT(sampledInput.idx0 != -1);
        return channel.sampler.outputValues[sampledInput.idx0];
    }

    PropertyType v0 = channel.sampler.outputValues[sampledInput.idx0 * stride + offset];
    PropertyType v1 = channel.sampler.outputValues[sampledInput.idx1 * stride + offset];

    PropertyType value = v0;
    switch (channel.sampler.interpolation) {
    case AnimationInterpolation::Linear:
        value = ark::lerp(v0, v1, sampledInput.interpolation);
        break;
    case AnimationInterpolation::Step:
        value = (sampledInput.interpolation > 0.5f) ? v1 : v0;
        break;
    case AnimationInterpolation::CubicSpline:
        NOT_YET_IMPLEMENTED();
        break;
    }

    return value;
}
