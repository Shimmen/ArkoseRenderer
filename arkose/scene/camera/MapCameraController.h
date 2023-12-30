#pragma once

#include "scene/camera/CameraController.h"
#include <ark/matrix.h>
#include <ark/quaternion.h>
#include <ark/vector.h>
#include <optional>

// A PDX-style top-down ish camera for panning and zooming over a "map" along the xy-plane
class MapCameraController : public CameraController {
public:
    void update(const Input&, float deltaTime) override;

    void takeControlOfCamera(Camera&) override;
    Camera* relinquishControl() override;

    float mapDistance() const { return m_mapDistance; }
    void setMapDistance(float mapDistance) { m_mapDistance = mapDistance; }

    float maxSpeed() const { return m_maxSpeed; }
    void setMaxSpeed(float maxSpeed) { m_maxSpeed = maxSpeed; }

    void setTargetFocusDepth(float) override {}; // not supported
    void clearTargetFocusDepth() override {};

private:
    vec3 m_velocity {};
    float m_maxSpeed { 200.0f };

    float m_targetFieldOfView { -1.0f };

    float m_mapDistance { 100.0f };

    static constexpr float TimeToMaxSpeed { 0.15f };
    static constexpr float TimeFromMaxSpeed { 0.20f };
    static constexpr float StopThreshold { 0.01f };

    static constexpr float ZoomSensitivity { 0.15f };
    static constexpr float MinFieldOfView { ark::toRadians(5.0f) };
    static constexpr float MaxFieldOfView { ark::toRadians(60.0f) };

};
