#include "FpsCamera.h"

#include <moos/transform.h>

void FpsCamera::setMaxSpeed(float newMaxSpeed)
{
    maxSpeed = newMaxSpeed;
}

void FpsCamera::update(const Input& input, float dt)
{
    // Apply acceleration from input

    vec3 acceleration { 0.0f };

    vec2 controllerMovement = input.leftStick();
    bool usingController = length(controllerMovement) > 0.0f;
    acceleration += controllerMovement.x * moos::globalRight;
    acceleration += controllerMovement.y * moos::globalForward;

    if (input.isKeyDown(Key::W))
        acceleration += moos::globalForward;
    if (input.isKeyDown(Key::S))
        acceleration -= moos::globalForward;

    if (input.isKeyDown(Key::D))
        acceleration += moos::globalRight;
    if (input.isKeyDown(Key::A))
        acceleration -= moos::globalRight;

    if (input.isKeyDown(Key::Space) || input.isKeyDown(Key::E))
        acceleration += moos::globalUp;
    if (input.isKeyDown(Key::LeftShift) || input.isKeyDown(Key::Q))
        acceleration -= moos::globalUp;

    if (usingController) {
        m_velocity += moos::rotateVector(orientation(), acceleration);
    } else {
        if (moos::length2(acceleration) > 0.01f && !input.isGuiUsingKeyboard()) {
            acceleration = normalize(acceleration) * (maxSpeed / timeToMaxSpeed) * dt;
            m_velocity += moos::rotateVector(orientation(), acceleration);
        } else {
            // If no input and movement to acceleration decelerate instead
            if (length2(m_velocity) < stopThreshold) {
                m_velocity = vec3(0.0f);
            } else {
                vec3 deaccel = -normalize(m_velocity) * (maxSpeed / timeFromMaxSpeed) * dt;
                m_velocity += deaccel;
            }
        }
    }

    // Apply velocity to position

    float speed = length(m_velocity);
    if (speed > 0.0f) {
        speed = moos::clamp(speed, 0.0f, maxSpeed);
        m_velocity = normalize(m_velocity) * speed;
        moveBy(m_velocity * dt);
    }

    // Calculate rotation velocity from input

    // Make rotations less sensitive when zoomed in
    float fovMultiplier = 0.2f + ((fieldOfView() - minFieldOfView) / (maxFieldOfView - minFieldOfView)) * 0.8f;

    vec2 controllerRotation = 0.3f * input.rightStick();
    m_pitchYawRoll.x -= controllerRotation.x * fovMultiplier * dt;
    m_pitchYawRoll.y += controllerRotation.y * fovMultiplier * dt;

    if (input.isButtonDown(Button::Right) && !input.isGuiUsingMouse()) {
        // Screen size independent but also aspect ratio dependent!
        vec2 mouseDelta = input.mouseDelta() / float(viewportSize().width());

        m_pitchYawRoll.x += -mouseDelta.x * rotationMultiplier * fovMultiplier * dt;
        m_pitchYawRoll.y += -mouseDelta.y * rotationMultiplier * fovMultiplier * dt;
    }

    // Calculate banking due to movement

    vec3 right = rotateVector(orientation(), moos::globalRight);
    vec3 forward = rotateVector(orientation(), moos::globalForward);

    if (speed > 0.0f) {
        auto direction = m_velocity / speed;
        float speedAlongRight = dot(direction, right) * speed;
        float signOrZeroSpeed = float(speedAlongRight > 0.0f) - float(speedAlongRight < 0.0f);
        float bankAmountSpeed = std::abs(speedAlongRight) / maxSpeed * 2.0f;

        float rotationAlongY = m_pitchYawRoll.x;
        float signOrZeroRotation = float(rotationAlongY > 0.0f) - float(rotationAlongY < 0.0f);
        float bankAmountRotation = moos::clamp(std::abs(rotationAlongY) * 100.0f, 0.0f, 3.0f);

        float targetBank = ((signOrZeroSpeed * bankAmountSpeed) + (signOrZeroRotation * bankAmountRotation)) * baselineBankAngle;
        m_pitchYawRoll.z = moos::lerp(m_pitchYawRoll.z, targetBank, 1.0f - pow(0.35f, dt));
    }

    // Damp rotation continuously

    m_pitchYawRoll *= pow(rotationDampening, dt);

    // Apply rotation

    quat newOrientation = axisAngle(right, m_pitchYawRoll.y) * orientation();
    newOrientation = axisAngle(vec3(0, 1, 0), m_pitchYawRoll.x) * newOrientation;
    setOrientation(newOrientation);

    m_bankingOrientation = moos::axisAngle(forward, m_pitchYawRoll.z);

    // Apply zoom

    if (!input.isGuiUsingMouse()) {
        m_targetFieldOfView += -input.scrollDelta() * zoomSensitivity;
        m_targetFieldOfView = moos::clamp(m_targetFieldOfView, minFieldOfView, maxFieldOfView);
    }
    float fov = moos::lerp(fieldOfView(), m_targetFieldOfView, 1.0f - pow(0.01f, dt));
    setFieldOfView(fov);

    // Create the view matrix

    auto preAdjustedUp = rotateVector(orientation(), vec3(0, 1, 0));
    auto up = rotateVector(m_bankingOrientation, preAdjustedUp);

    vec3 target = position() + forward;
    setViewFromWorld(moos::lookAt(position(), target, up));

    // Create the projection matrix

    setProjectionFromView(moos::perspectiveProjectionToVulkanClipSpace(fieldOfView(), aspectRatio(), zNear, zFar));
}
