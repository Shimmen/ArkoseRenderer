#include "FpsCameraController.h"

#include "core/Assert.h"
#include "input/Input.h"
#include "scene/camera/Camera.h"
#include <ark/transform.h>

void FpsCameraController::takeControlOfCamera(Camera& camera)
{
    CameraController::takeControlOfCamera(camera);
    m_targetFieldOfView = camera.fieldOfView();
}

Camera* FpsCameraController::relinquishControl()
{
    return CameraController::relinquishControl();
}

void FpsCameraController::setTargetFocusDepth(float focusDepth)
{
    m_targetFocusDepth = focusDepth;
}

void FpsCameraController::clearTargetFocusDepth()
{
    m_targetFocusDepth = std::nullopt;
}

void FpsCameraController::update(const Input& input, float dt)
{
    ARKOSE_ASSERT(isCurrentlyControllingCamera());
    Camera& camera = *controlledCamera();

    // Apply acceleration from input

    vec3 acceleration { 0.0f };

    vec2 controllerMovement = input.leftStick();
    bool usingController = length(controllerMovement) > 0.0f;
    acceleration += controllerMovement.x * ark::globalRight;
    acceleration += controllerMovement.y * ark::globalForward;

    if (input.isButtonDown(Button::Right)) {
        if (input.isKeyDown(Key::W))
            acceleration += ark::globalForward;
        if (input.isKeyDown(Key::S))
            acceleration -= ark::globalForward;

        if (input.isKeyDown(Key::D))
            acceleration += ark::globalRight;
        if (input.isKeyDown(Key::A))
            acceleration -= ark::globalRight;

        if (input.isKeyDown(Key::Space) || input.isKeyDown(Key::E))
            acceleration += ark::globalUp;
        if (input.isKeyDown(Key::LeftShift) || input.isKeyDown(Key::Q))
            acceleration -= ark::globalUp;
    }

    if (usingController) {
        m_velocity += ark::rotateVector(camera.orientation(), acceleration);
    } else {
        if (ark::length2(acceleration) > 0.01f && !input.isGuiUsingKeyboard()) {
            acceleration = normalize(acceleration) * (m_maxSpeed / TimeToMaxSpeed) * dt;
            m_velocity += ark::rotateVector(camera.orientation(), acceleration);
        } else {
            // If no input and movement to acceleration decelerate instead
            if (length2(m_velocity) < StopThreshold) {
                m_velocity = vec3(0.0f);
            } else {
                vec3 deaccel = -normalize(m_velocity) * (m_maxSpeed / TimeFromMaxSpeed) * dt;
                m_velocity += deaccel;
            }
        }
    }

    // Apply velocity to position

    float speed = length(m_velocity);
    if (speed > 0.0f) {
        speed = ark::clamp(speed, 0.0f, m_maxSpeed);
        m_velocity = normalize(m_velocity) * speed;
        camera.moveBy(m_velocity * dt);
    }

    // Calculate rotation velocity from input

    // Make rotations less sensitive when zoomed in
    float fovMultiplier = 0.2f + ((camera.fieldOfView() - MinFieldOfView) / (MaxFieldOfView - MinFieldOfView)) * 0.8f;

    vec2 controllerRotation = 0.3f * input.rightStick();
    m_pitchYawRoll.x -= controllerRotation.x * fovMultiplier * dt;
    m_pitchYawRoll.y += controllerRotation.y * fovMultiplier * dt;

    if (input.isButtonDown(Button::Right) && !input.isGuiUsingMouse()) {
        // Screen size independent but also aspect ratio dependent!
        u32 sizeAdjust = camera.renderingToWindow() ? camera.targetWindowSize().width() : camera.viewport().width();
        vec2 mouseDelta = input.mouseDelta() / static_cast<float>(sizeAdjust);

        m_pitchYawRoll.x += -mouseDelta.x * RotationMultiplier * fovMultiplier * dt;
        m_pitchYawRoll.y += -mouseDelta.y * RotationMultiplier * fovMultiplier * dt;
    }

    // Calculate banking due to movement

    vec3 right = rotateVector(camera.orientation(), ark::globalRight);
    vec3 forward = rotateVector(camera.orientation(), ark::globalForward);

    if (speed > 0.0f) {
        auto direction = m_velocity / speed;
        float speedAlongRight = dot(direction, right) * speed;
        float signOrZeroSpeed = float(speedAlongRight > 0.0f) - float(speedAlongRight < 0.0f);
        float bankAmountSpeed = std::abs(speedAlongRight) / m_maxSpeed * 2.0f;

        float rotationAlongY = m_pitchYawRoll.x;
        float signOrZeroRotation = float(rotationAlongY > 0.0f) - float(rotationAlongY < 0.0f);
        float bankAmountRotation = ark::clamp(std::abs(rotationAlongY) * 100.0f, 0.0f, 3.0f);

        float targetBank = ((signOrZeroSpeed * bankAmountSpeed) + (signOrZeroRotation * bankAmountRotation)) * BaselineBankAngle;
        m_pitchYawRoll.z = ark::lerp(m_pitchYawRoll.z, targetBank, 1.0f - pow(0.35f, dt));
    }

    // Damp rotation continuously

    m_pitchYawRoll *= pow(RotationDampening, dt);

    // Apply rotation

    quat newOrientation = axisAngle(right, m_pitchYawRoll.y) * camera.orientation();
    newOrientation = axisAngle(vec3(0, 1, 0), m_pitchYawRoll.x) * newOrientation;
    camera.setOrientation(newOrientation);

    m_bankingOrientation = ark::axisAngle(forward, m_pitchYawRoll.z);

    // Apply zoom

    if (!input.isGuiUsingMouse() && !input.isKeyDown(Key::LeftShift)) {
        m_targetFieldOfView += -input.scrollDelta() * ZoomSensitivity;
        m_targetFieldOfView = ark::clamp(m_targetFieldOfView, MinFieldOfView, MaxFieldOfView);
    }
    float fov = ark::lerp(camera.fieldOfView(), m_targetFieldOfView, 1.0f - pow(0.01f, dt));
    camera.setFieldOfView(fov);

    // Apply focus adjustments

    if (m_targetFocusDepth.has_value()) {
        if (!input.isGuiUsingMouse() && input.isKeyDown(Key::LeftShift)) {
            m_targetFocusDepth.value() += 0.008f * input.scrollDelta();
        }
        float focusDepth = ark::lerp(camera.focusDepth(), m_targetFocusDepth.value(), 1.0f - std::exp2(-m_focusDepthLerpSpeed * dt));
        camera.setFocusDepth(focusDepth);
    }

    // Create the view matrix

    auto preAdjustedUp = rotateVector(camera.orientation(), vec3(0, 1, 0));
    auto up = rotateVector(m_bankingOrientation, preAdjustedUp);

    vec3 target = camera.position() + forward;
    camera.setViewFromWorld(ark::lookAt(camera.position(), target, up));

    // Create the projection matrix

    camera.setProjectionFromView(ark::perspectiveProjectionToVulkanClipSpace(camera.fieldOfView(), camera.aspectRatio(), camera.nearClipPlane(), camera.farClipPlane()));

    // Finalize

    camera.finalizeModifications();
}
