#include "Animation.h"

#include "scene/MeshInstance.h"
#include "scene/Transform.h"
#include <cmath>

Animation::Animation(AnimationAsset const* asset)
    : m_asset(asset)
{
}

Animation::~Animation()
{
}

std::unique_ptr<Animation> Animation::bind(AnimationAsset* animationAsset, SkeletalMeshInstance& skeletalMeshInstance)
{
    auto animation = std::make_unique<Animation>(animationAsset);
    animation->setSkeletalMeshInstance(skeletalMeshInstance);
    return animation;
}

void Animation::setSkeletalMeshInstance(SkeletalMeshInstance& skeletalMeshInstance)
{
    m_skeletalMeshInstance = &skeletalMeshInstance;
}

void Animation::tick(float deltaTime)
{
    size_t numInputTracks = m_asset->inputTracks.size();

    std::vector<SampledInputTrack> sampledInputTracks {};
    sampledInputTracks.resize(numInputTracks);

    for (size_t idx = 0; idx < numInputTracks; ++idx) {
        std::vector<float> const& inputTrack = m_asset->inputTracks[idx];
        sampledInputTracks[idx] = evaluateInputTrack(idx, inputTrack);
    }

    //for (AnimationChannelAsset<f32> const& channel : m_asset->floatPropertyChannels) {
    //    // We don't yet have any float properties that can be animated!
    //    NOT_YET_IMPLEMENTED();
    //}

    //for (AnimationChannelAsset<vec2> const& channel : m_asset->float2PropertyChannels) {
    //    // We don't yet have any float2 properties that can be animated!
    //    NOT_YET_IMPLEMENTED();
    //}

    for (AnimationChannelAsset<vec3> const& channel : m_asset->float3PropertyChannels) {
        // NOTE: Float3 properties can only be used for translation and scale, possibly also rotation using euler angles
        ARKOSE_ASSERT(channel.targetProperty == AnimationTargetProperty::Translation || channel.targetProperty == AnimationTargetProperty::Scale);

        SampledInputTrack const& sampledInput = sampledInputTracks[channel.sampler.inputTrackIdx];
        vec3 value = evaluateAnimationChannel(sampledInput, channel);

        if (Transform* transform = findTransformForTarget(channel.targetReference)) {
            if (channel.targetProperty == AnimationTargetProperty::Translation) {
                transform->setTranslation(value);
            } else if (channel.targetProperty == AnimationTargetProperty::Scale) {
                transform->setScale(value);
            }
        }
    }

    for (AnimationChannelAsset<vec4> const& channel : m_asset->float4PropertyChannels) {
        // NOTE: Float4 properties can only be used for orientation/rotation
        ARKOSE_ASSERT(channel.targetProperty == AnimationTargetProperty::Rotation);

        SampledInputTrack const& sampledInput = sampledInputTracks[channel.sampler.inputTrackIdx];
        vec4 value = evaluateAnimationChannel(sampledInput, channel);

        if (Transform* transform = findTransformForTarget(channel.targetReference)) {
            quat valueAsQuat = quat(value.xyz(), value.w);
            transform->setOrientation(valueAsQuat);
        }
    }

    m_animationTime += deltaTime;
}

void Animation::reset()
{
    m_animationTime = 0.0f;
}

Animation::SampledInputTrack Animation::evaluateInputTrack(size_t inputTrackIdx, std::vector<float> const& inputTrack)
{
    ARKOSE_ASSERT(inputTrack.size() >= 1);

    float inputTrackStart = inputTrack.front();
    float inputTrackEnd = inputTrack.back();
    float inputTrackLength = inputTrackEnd - inputTrackStart;

    float inputTrackTime = m_animationTime;

    if (playbackMode() == PlaybackMode::OneShot) {
        if (inputTrackTime <= inputTrackStart) {
            return SampledInputTrack { .idx0 = 0 };
        } else if (inputTrackTime >= inputTrackEnd) {
            i32 lastIdx = static_cast<i32>(inputTrack.size()) - 1;
            return SampledInputTrack { .idx0 = lastIdx };
        }
    } else if (playbackMode() == PlaybackMode::Looping) {
        while (inputTrackTime < inputTrackStart) {
            inputTrackTime += inputTrackLength;
        }
        inputTrackTime = inputTrackStart + std::fmod(inputTrackTime - inputTrackStart, inputTrackLength);
    }

    // Find the first input track index
    // TODO: Binary search and/or cache last value
    i32 startIdx = 0;
    while (inputTrack[startIdx] < inputTrackTime) {
        startIdx += 1;
    }
    // We overshoot by one..
    startIdx = startIdx - 1;

    // TODO: Handle out-of-bounds case! Or wait, should we even need to handle it here after the previous precautions?!
    i32 endIdx = startIdx + 1;

    float startTime = inputTrack[startIdx];
    float endTime = inputTrack[endIdx];
    float lerpValue = ark::inverseLerp(inputTrackTime, startTime, endTime);

    return SampledInputTrack { .idx0 = startIdx,
                               .idx1 = endIdx,
                               .interpolation = lerpValue };
}

Transform* Animation::findTransformForTarget(std::string const& targetReference)
{
    if (m_skeletalMeshInstance) {
        return m_skeletalMeshInstance->findTransformForJoint(targetReference);
    }

    return nullptr;
}
