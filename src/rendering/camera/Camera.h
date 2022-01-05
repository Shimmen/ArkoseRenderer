#pragma once

#pragma once

#include "utility/Badge.h"
#include "utility/Extent.h"
#include "utility/Input.h"
#include <moos/matrix.h>
#include <moos/quaternion.h>
#include <moos/vector.h>
#include <optional>

class Scene;

class Camera {
public:
    Camera() = default;
    ~Camera() = default;

    virtual void newFrame(Badge<Scene>, Extent2D viewportSize);
    virtual void update(const Input&, float deltaTime) {}

    void renderExposureGUI();
    void renderManualExposureGUI();
    void renderAutomaticExposureGUI();

    bool hasChangedSinceLastFrame() const { return m_modified; }

    void lookAt(const vec3& position, const vec3& target, const vec3& up = moos::globalY);

    Extent2D viewportSize() const { return m_viewportSize; }
    float aspectRatio() const;

    float fieldOfView() const { return m_fieldOfView; }
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

    static constexpr float zNear { 0.25f };
    static constexpr float zFar { 10000.0f };

    // Default manual values according to the "sunny 16 rule" (https://en.wikipedia.org/wiki/Sunny_16_rule)
    float aperture { 16.0f }; // i.e. f/16
    float iso { 400.0f };
    float shutterSpeed { 1.0f / iso };

    bool useAutomaticExposure { true };
    float exposureCompensation { 0.0f };
    float adaptionRate { 0.0018f };

protected:
    void setViewFromWorld(mat4);
    void setProjectionFromView(mat4);

    void markAsModified() { m_modified = true; }

private:
    vec3 m_position {};
    quat m_orientation {};
    float m_fieldOfView { moos::toRadians(60.0f) };

    mat4 m_viewFromWorld {};
    mat4 m_projectionFromView {};
    Extent2D m_viewportSize {};

    std::optional<mat4> m_previousFrameViewFromWorld { std::nullopt };
    std::optional<mat4> m_previousFrameProjectionFromView { std::nullopt };

    bool m_modified { true };
};
