#pragma once

#pragma once

#include <ark/badge.h>
#include "core/math/Frustum.h"
#include "utility/Extent.h"
#include "utility/Input.h"
#include <ark/matrix.h>
#include <ark/quaternion.h>
#include <ark/vector.h>
#include <optional>

class CameraController;
class Scene;

class Camera final {
public:
    Camera() = default;
    ~Camera() = default;

    void preRender(Badge<Scene>);
    void postRender(Badge<Scene>);

    void drawGui(bool includeContainingWindow = false);
    void drawExposureGui();
    void drawManualExposureGui();
    void drawAutomaticExposureGui();

    bool hasChangedSinceLastFrame() const { return m_modified; }

    void lookAt(const vec3& position, const vec3& target, const vec3& up = ark::globalY);

    Extent2D viewport() const { return m_viewportSize; }
    void setViewport(Extent2D viewportSize) { m_viewportSize = viewportSize; }
    float aspectRatio() const;

    float focalLengthMeters() const { return m_focalLength / 1000.0f; }
    float focalLengthMillimeters() const { return m_focalLength; }
    void setFocalLength(float);

    enum class ExposureMode {
        Auto,
        Manual,
    };

    float shutterSpeed() const { return m_shutterSpeed; }
    float aperture() const { return m_fNumber; }
    float fNumber() const { return m_fNumber; }
    float ISO() const { return m_iso; }

    enum class FocusMode {
        Auto,
        Manual,
    };

    float focusDepth() const { return m_focusDepth; }
    void setFocusDepth(float focusDepth);

    float circleOfConfusionMmToPxFactor() const;

    // NOTE: *horizontal* field of view
    float fieldOfView() const;
    void setFieldOfView(float);

    void setExposureMode(ExposureMode);
    void setManualExposureParameters(float fNumber, float shutterSpeed, float ISO);

    float exposureCompensation() const;
    void setExposureCompensation(float EC);

    void setAutoExposureAdaptionRate(float);
    float autoExposureAdaptionRate() const { return m_adaptionRate; }

    float EV100() const { return calculateEV100(fNumber(), shutterSpeed(), ISO()); }
    float exposure() const;

    float filmGrainGain() const;

    vec3 position() const { return m_position; }
    void setPosition(vec3);
    void moveBy(vec3);

    quat orientation() const { return m_orientation; }
    void setOrientation(quat);

    vec3 forward() const { return ark::rotateVector(orientation(), ark::globalForward); }
    vec3 right() const { return ark::rotateVector(orientation(), ark::globalRight); }
    vec3 up() const { return ark::rotateVector(orientation(), ark::globalUp); }

    void setNearAndFarPlanes(float zNear, float zFar);
    [[nodiscard]] float zNear() const { return m_zNear; }
    [[nodiscard]] float zFar() const { return m_zFar; }

    [[nodiscard]] geometry::Frustum const& frustum() const { return m_cullingFrustum; }

    [[nodiscard]] mat4 viewMatrix() const { return m_viewFromWorld; }
    [[nodiscard]] mat4 projectionMatrix() const { return m_projectionFromView; }
    [[nodiscard]] mat4 viewProjectionMatrix() const { return projectionMatrix() * viewMatrix(); }

    [[nodiscard]] mat4 previousFrameViewMatrix() const { return m_previousFrameViewFromWorld.value_or(viewMatrix()); }
    [[nodiscard]] mat4 previousFrameProjectionMatrix() const { return m_previousFrameProjectionFromView.value_or(projectionMatrix()); }
    [[nodiscard]] mat4 previousFrameViewProjectionMatrix() const { return previousFrameProjectionMatrix() * previousFrameViewMatrix(); }

    mat4 pixelProjectionMatrix() const;

    bool isFrustumJitteringEnabled() const { return m_frustumJitteringEnabled; }
    void setFrustumJitteringEnabled(bool enabled) { m_frustumJitteringEnabled = enabled; }
    [[nodiscard]] vec2 frustumJitterPixelOffset() const { return m_frustumJitterPixelOffset; }
    [[nodiscard]] vec2 previousFrameFrustumJitterPixelOffset() const { return m_previousFrameFrustumJitterPixelOffset.value_or(vec2(0.0f, 0.0f)); }
    [[nodiscard]] vec2 frustumJitterUVCorrection() const;

    void setViewFromWorld(mat4);
    void setProjectionFromView(mat4);

    void finalizeModifications();

    void setController(Badge<CameraController>, CameraController* controller) { m_controller = controller; }
    CameraController* controller() { return m_controller; }

    template<class Archive>
    void serialize(Archive&);

protected:
    void markAsModified() { m_modified = true; }

    static float calculateEV100(float fNumber, float shutterSpeed, float ISO);
    static float calculateManualExposure(float fNumber, float shutterSpeed, float ISO);

    static float calculateFieldOfView(float focalLenght, vec2 sensorSize);
    static float calculateFocalLength(float fieldOfView, vec2 sensorSize);

    static vec2 calculateAdjustedSensorSize(vec2 sensorSize, Extent2D viewportSize);
    static vec2 calculateSensorPixelSize(vec2 sensorSize, Extent2D viewportSize);
    
    static float calculateAcceptableCircleOfConfusion(vec2 sensorSize, Extent2D viewportSize);
    static float convertCircleOfConfusionToPixelUnits(float circleOfConfusion, vec2 sensorSize, Extent2D viewportSize);

    // I.e. the depth (m) that would be considered in focus about the focus depth
    static float calculateDepthOfField(float acceptibleCircleOfConfusionMM, float focalLengthMM, float fNumber, float focusDepthM);
    static vec2 calculateDepthOfFieldRange(float focusDepthM, float depthOfField);

    void debugRenderCullingFrustum() const;

private:

    ////////////////////////////////////////////////////////////////////////////
    // Focus parameters

    FocusMode m_focusMode { FocusMode::Manual };

    float m_focalLength { 30.0f }; // millimeters (mm)
    float m_focusDepth { 5.0f }; // meters (m)

    // i.e. 35mm film. We assume no crop factor for now and base everything on this
    vec2 m_sensorSize { 36.0f, 24.0f };

    ////////////////////////////////////////////////////////////////////////////
    // Exposure paramers

    ExposureMode m_exposureMode { ExposureMode::Manual };

    // Manual exposure

    // Default manual values according to the "sunny 16 rule" (https://en.wikipedia.org/wiki/Sunny_16_rule)
    float m_fNumber { 16.0f }; // i.e. the denominator of f/XX, the aperture settings
    float m_iso { 400.0f };
    float m_shutterSpeed { 1.0f / m_iso };

    // Auto-exposure
    
    float m_exposureCompensation { 0.0f };
    float m_adaptionRate { 0.0018f };

    ////////////////////////////////////////////////////////////////////////////
    // Physical position & orentation of the camera

    vec3 m_position {};
    quat m_orientation {};

    float m_zNear { 0.25f };
    float m_zFar { 10'000.0f };

    ////////////////////////////////////////////////////////////////////////////
    // Film grain control

    // Not very physically based, but hopefully a bit plausible..
    float m_filmGrainAtISO100 { 0.01f };
    float m_filmGrainAtISO3200 { 0.15f };

    ////////////////////////////////////////////////////////////////////////////
    // Culling

    mat4 m_cullingViewProjection {};
    geometry::Frustum m_cullingFrustum {};

    bool m_debugFreezeCamera { false };
    bool m_debugRenderCullingFrustum { true };

    ////////////////////////////////////////////////////////////////////////////
    // Meta

    mat4 m_viewFromWorld {};
    mat4 m_projectionFromView {};
    Extent2D m_viewportSize {};

    bool m_frustumJitteringEnabled { false };
    vec2 m_frustumJitterPixelOffset {};
    size_t m_frameIndex { 0 };

    std::optional<mat4> m_previousFrameViewFromWorld { std::nullopt };
    std::optional<mat4> m_previousFrameProjectionFromView { std::nullopt };
    std::optional<vec2> m_previousFrameFrustumJitterPixelOffset { std::nullopt };

    CameraController* m_controller { nullptr };

    bool m_modified { true };
};


////////////////////////////////////////////////////////////////////////////////
// Serialization

#include <cereal/cereal.hpp>
#include <cereal/types/optional.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/vector.hpp>

template<class Archive>
std::string save_minimal(Archive const&, Camera::FocusMode const& focusMode)
{
    if (focusMode == Camera::FocusMode::Auto) {
        return "Auto";
    } else if (focusMode == Camera::FocusMode::Manual) {
        return "Manual";
    } else {
        ASSERT_NOT_REACHED();
    }
}

template<class Archive>
void load_minimal(Archive const&, Camera::FocusMode& focusMode, std::string const& value)
{
    if (value == "Auto") {
        focusMode = Camera::FocusMode::Auto;
    } else if (value == "Manual") {
        focusMode = Camera::FocusMode::Manual;
    } else {
        ASSERT_NOT_REACHED();
    }
}

template<class Archive>
std::string save_minimal(Archive const&, Camera::ExposureMode const& exposureMode)
{
    if (exposureMode == Camera::ExposureMode::Auto) {
        return "Auto";
    } else if (exposureMode == Camera::ExposureMode::Manual) {
        return "Manual";
    } else {
        ASSERT_NOT_REACHED();
    }
}

template<class Archive>
void load_minimal(Archive const&, Camera::ExposureMode& exposureMode, std::string const& value)
{
    if (value == "Auto") {
        exposureMode = Camera::ExposureMode::Auto;
    } else if (value == "Manual") {
        exposureMode = Camera::ExposureMode::Manual;
    } else {
        ASSERT_NOT_REACHED();
    }
}

template<class Archive>
void Camera::serialize(Archive& archive)
{
    archive(cereal::make_nvp("position", m_position));
    archive(cereal::make_nvp("orientation", m_orientation));

    archive(cereal::make_nvp("focusMode", m_focusMode));
    archive(cereal::make_nvp("focalLength", m_focalLength));
    archive(cereal::make_nvp("focusDepth", m_focusDepth));
    archive(cereal::make_nvp("sensorSize", m_sensorSize));

    archive(cereal::make_nvp("exposureMode", m_exposureMode));
    
    archive(cereal::make_nvp("fNumber", m_fNumber));
    archive(cereal::make_nvp("iso", m_iso));
    archive(cereal::make_nvp("shutterSpeed", m_shutterSpeed));

    archive(cereal::make_nvp("exposureCompensation", m_exposureCompensation));
    archive(cereal::make_nvp("adaptionRate", m_adaptionRate));
}
