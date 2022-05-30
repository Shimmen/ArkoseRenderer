#pragma once

#pragma once

#include "core/Badge.h"
#include "utility/Extent.h"
#include "utility/Input.h"
#include <moos/matrix.h>
#include <moos/quaternion.h>
#include <moos/vector.h>
#include <optional>

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

    void lookAt(const vec3& position, const vec3& target, const vec3& up = moos::globalY);

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

    vec3 position() const { return m_position; }
    void setPosition(vec3);
    void moveBy(vec3);

    quat orientation() const { return m_orientation; }
    void setOrientation(quat);

    vec3 forward() const { return moos::rotateVector(orientation(), moos::globalForward); }
    vec3 right() const { return moos::rotateVector(orientation(), moos::globalRight); }
    vec3 up() const { return moos::rotateVector(orientation(), moos::globalUp); }

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

    static constexpr float zNear { 0.25f };
    static constexpr float zFar { 10000.0f };

    void setViewFromWorld(mat4);
    void setProjectionFromView(mat4);

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

    bool m_modified { true };
};
