#pragma once

#include "scene/camera/CameraController.h"
#include <ark/matrix.h>
#include <ark/vector.h>
#include <ark/quaternion.h>
#include <optional>

class FpsCameraController : public CameraController {
public:
    void update(const Input&, float deltaTime) override;

    void takeControlOfCamera(Camera&) override;
    Camera* relinquishControl() override;

    float maxSpeed() const { return m_maxSpeed; }
    void setMaxSpeed(float maxSpeed) { m_maxSpeed = maxSpeed; }

    void setTargetFocusDepth(float) override;
    void clearTargetFocusDepth() override;

private:
    vec3 m_velocity {};
    float m_maxSpeed { 10.0f };

    vec3 m_pitchYawRoll {};
    quat m_bankingOrientation { { 0, 0, 0 }, 1 };
    
    float m_targetFieldOfView { -1.0f };

    std::optional<float> m_targetFocusDepth {};
    float m_focusDepthLerpSpeed { 10.0f };

    static constexpr float TimeToMaxSpeed { 0.25f };
    static constexpr float TimeFromMaxSpeed { 0.60f };
    static constexpr float StopThreshold { 0.02f };

    static constexpr float RotationMultiplier { 30.0f };
    static constexpr float RotationDampening { 0.000005f };

    static constexpr float ZoomSensitivity { 0.15f };
    static constexpr float MinFieldOfView { ark::toRadians(5.0f) };
    static constexpr float MaxFieldOfView { ark::toRadians(60.0f) };

    static constexpr float BaselineBankAngle { ark::toRadians(30.0f) };

};