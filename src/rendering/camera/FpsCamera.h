#pragma once

#include "utility/Extent.h"
#include "utility/Input.h"
#include <moos/matrix.h>
#include <moos/quaternion.h>
#include <moos/vector.h>

class FpsCamera {
public:
    FpsCamera() = default;
    ~FpsCamera() = default;

    void setMaxSpeed(float);

    void update(const Input&, const Extent2D& screenExtent, float deltaTime);

    void setDidModify(bool);
    bool didModify() const;

    void lookAt(const vec3& position, const vec3& target, const vec3& up = moos::globalY);

    vec3 position() const { return m_position; }
    void setPosition(vec3 p) { m_position = p; }

    quat orientation() const { return m_orientation; }
    void setOrientation(quat q) { m_orientation = q; }

    [[nodiscard]] mat4 viewMatrix() const { return m_viewFromWorld; }
    [[nodiscard]] mat4 projectionMatrix() const { return m_projectionFromView; }

    // Default manual values according to the "sunny 16 rule" (https://en.wikipedia.org/wiki/Sunny_16_rule)
    float aperture { 16.0f }; // i.e. f/16
    float iso { 400.0f };
    float shutterSpeed { 1.0f / iso };

    bool useAutomaticExposure { true };
    float exposureCompensation { 0.0f };
    float adaptionRate { 0.0018f };

private:
    vec3 m_position {};
    vec3 m_velocity {};

    quat m_orientation {};
    vec3 m_pitchYawRoll {};
    quat m_bankingOrientation { { 0, 0, 0 }, 1 };

    float m_fieldOfView { moos::toRadians(60.0f) };
    float m_targetFieldOfView { m_fieldOfView };

    mat4 m_viewFromWorld {};
    mat4 m_projectionFromView {};

    bool m_didModify { true };

    static constexpr float zNear { 0.25f };

    float maxSpeed { 10.0f };
    static constexpr float timeToMaxSpeed { 0.25f };
    static constexpr float timeFromMaxSpeed { 0.60f };
    static constexpr float stopThreshold { 0.02f };

    static constexpr float rotationMultiplier { 30.0f };
    static constexpr float rotationDampening { 0.000005f };

    static constexpr float zoomSensitivity { 0.15f };
    static constexpr float minFieldOfView { moos::toRadians(15.0f) };
    static constexpr float maxFieldOfView { moos::toRadians(60.0f) };

    static constexpr float baselineBankAngle { moos::toRadians(30.0f) };
};
