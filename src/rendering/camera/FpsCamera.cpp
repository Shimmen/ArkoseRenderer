#include "FpsCamera.h"

#include <moos/transform.h>

void FpsCamera::setMaxSpeed(float newMaxSpeed)
{
    maxSpeed = newMaxSpeed;
}

void FpsCamera::update(const Input& input, const Extent2D& viewportSize, float dt)
{
    m_didModify = false;

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
        m_velocity += moos::rotateVector(m_orientation, acceleration);
    } else {
        if (moos::length2(acceleration) > 0.01f && !input.isGuiUsingKeyboard()) {
            acceleration = normalize(acceleration) * (maxSpeed / timeToMaxSpeed) * dt;
            m_velocity += moos::rotateVector(m_orientation, acceleration);
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

        m_position += m_velocity * dt;
        m_didModify = true;
    }

    // Calculate rotation velocity from input

    vec3 prevPitchYawRoll = m_pitchYawRoll;

    // Make rotations less sensitive when zoomed in
    float fovMultiplier = 0.2f + ((m_fieldOfView - minFieldOfView) / (maxFieldOfView - minFieldOfView)) * 0.8f;

    vec2 controllerRotation = 0.3f * input.rightStick();
    m_pitchYawRoll.x -= controllerRotation.x * fovMultiplier * dt;
    m_pitchYawRoll.y += controllerRotation.y * fovMultiplier * dt;

    if (input.isButtonDown(Button::Right) && !input.isGuiUsingMouse()) {
        // Screen size independent but also aspect ratio dependent!
        vec2 mouseDelta = input.mouseDelta() / float(viewportSize.width());

        m_pitchYawRoll.x += -mouseDelta.x * rotationMultiplier * fovMultiplier * dt;
        m_pitchYawRoll.y += -mouseDelta.y * rotationMultiplier * fovMultiplier * dt;
    }

    // Calculate banking due to movement

    vec3 right = rotateVector(m_orientation, moos::globalRight);
    vec3 forward = rotateVector(m_orientation, moos::globalForward);

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

    if (length(m_pitchYawRoll - prevPitchYawRoll) > 1e-6f) {
        m_didModify = true;
    }

    // Apply rotation

    m_orientation = axisAngle(right, m_pitchYawRoll.y) * m_orientation;
    m_orientation = axisAngle(vec3(0, 1, 0), m_pitchYawRoll.x) * m_orientation;
    m_bankingOrientation = moos::axisAngle(forward, m_pitchYawRoll.z);

    // Apply zoom

    if (!input.isGuiUsingMouse()) {
        m_targetFieldOfView += -input.scrollDelta() * zoomSensitivity;
        m_targetFieldOfView = moos::clamp(m_targetFieldOfView, minFieldOfView, maxFieldOfView);
    }
    float fov = moos::lerp(m_fieldOfView, m_targetFieldOfView, 1.0f - pow(0.01f, dt));
    if (abs(fov - m_fieldOfView) > 1e-6f) {
        m_didModify = true;
    }
    m_fieldOfView = fov;

    // Create the view matrix

    auto preAdjustedUp = rotateVector(m_orientation, vec3(0, 1, 0));
    auto up = rotateVector(m_bankingOrientation, preAdjustedUp);

    vec3 target = m_position + forward;
    m_viewFromWorld = moos::lookAt(m_position, target, up);

    // Create the projection matrix

    float width = static_cast<float>(viewportSize.width());
    float height = static_cast<float>(viewportSize.height());
    float aspectRatio = (height > 1e-6f) ? (width / height) : 1.0f;
    m_projectionFromView = moos::perspectiveProjectionToVulkanClipSpace(m_fieldOfView, aspectRatio, zNear, zFar);
    m_currentViewportSize = viewportSize;
}

mat4 FpsCamera::pixelProjectionMatrix() const
{
    // Ensures e.g. NDC (1,1) projects to (width-1,height-1)
    float roundingPixelsX = (float)m_currentViewportSize.width() - 0.001f;
    float roundingPixelsY = (float)m_currentViewportSize.height() - 0.001f;

    mat4 pixelFromNDC = moos::scale(vec3(roundingPixelsX, roundingPixelsY, 1.0f)) * moos::translate(vec3(0.5f, 0.5f, 0.0f)) * moos::scale(vec3(0.5f, 0.5f, 1.0f));
    return pixelFromNDC * projectionMatrix();
}

void FpsCamera::setDidModify(bool value)
{
    m_didModify = value;
}

bool FpsCamera::didModify() const
{
    return m_didModify;
}

void FpsCamera::lookAt(const vec3& position, const vec3& target, const vec3& up)
{
    m_position = position;

    vec3 forward = normalize(target - position);
    // TODO: Apparently I never bothered to implement lookRotation ...
    //m_orientation = moos::lookRotation(direction, up);
    vec3 right = cross(forward, up);
    vec3 properUp = cross(right, forward);
    mat3 orientationMat = mat3(right, properUp, -forward);
    m_orientation = moos::quatFromMatrix(mat4(orientationMat));

    m_viewFromWorld = moos::lookAt(m_position, target, up);
    setDidModify(true);
}
