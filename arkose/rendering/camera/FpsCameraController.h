#pragma once

#include "rendering/camera/CameraController.h"
#include <moos/matrix.h>
#include <moos/vector.h>
#include <moos/quaternion.h>

class FpsCameraController : public CameraController {
public:
    void update(const Input&, float deltaTime) override;

    void takeControlOfCamera(Camera&) override;
    Camera* relinquishControl() override;

    float maxSpeed() const { return m_maxSpeed; }
    void setMaxSpeed(float maxSpeed) { m_maxSpeed = maxSpeed; }

private:
    vec3 m_velocity {};
    float m_maxSpeed { 10.0f };

    vec3 m_pitchYawRoll {};
    quat m_bankingOrientation { { 0, 0, 0 }, 1 };
    
    float m_targetFieldOfView { -1.0f };

    static constexpr float TimeToMaxSpeed { 0.25f };
    static constexpr float TimeFromMaxSpeed { 0.60f };
    static constexpr float StopThreshold { 0.02f };

    static constexpr float RotationMultiplier { 30.0f };
    static constexpr float RotationDampening { 0.000005f };

    static constexpr float ZoomSensitivity { 0.15f };
    static constexpr float MinFieldOfView { moos::toRadians(15.0f) };
    static constexpr float MaxFieldOfView { moos::toRadians(60.0f) };

    static constexpr float BaselineBankAngle { moos::toRadians(30.0f) };

};