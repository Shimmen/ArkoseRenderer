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

    // NOTE: *horizontal* field of view
    float fieldOfView() const;
    void setFieldOfView(float);

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

    // Default manual values according to the "sunny 16 rule" (https://en.wikipedia.org/wiki/Sunny_16_rule)
    float aperture { 16.0f }; // i.e. f-number, i.e. the denominator of f/XX
    float iso { 400.0f };
    float shutterSpeed { 1.0f / iso };

    bool useAutomaticExposure { true };
    float exposureCompensation { 0.0f };
    float adaptionRate { 0.0018f };

    void setViewFromWorld(mat4);
    void setProjectionFromView(mat4);

protected:
    void markAsModified() { m_modified = true; }

    float calculateFieldOfView(float focalLenght) const;
    float calculateFocalLength(float fieldOfView) const;

private:

    float m_focalLength { 30.0f }; // millimeters (mm)

    // i.e. 35mm film. We assume no crop factor for now and base everything on this
    vec2 m_sensorSize { 36.0f, 24.0f };

    //

    vec3 m_position {};
    quat m_orientation {};

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
