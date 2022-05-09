#pragma once

#include "rendering/camera/Camera.h"
#include "utility/Extent.h"
#include "utility/Input.h"
#include <moos/matrix.h>
#include <moos/quaternion.h>
#include <moos/vector.h>
#include <optional>

class FpsCamera final : public Camera {
public:
    FpsCamera() = default;
    ~FpsCamera() = default;

    void setMaxSpeed(float);

    void update(const Input&, float deltaTime) override;

protected:
    vec3 m_velocity {};
    vec3 m_pitchYawRoll {};
    quat m_bankingOrientation { { 0, 0, 0 }, 1 };
    float m_targetFieldOfView { fieldOfView() };

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
