#include "MapCameraController.h"

#include "core/Assert.h"
#include "system/Input.h"
#include "scene/camera/Camera.h"
#include <ark/transform.h>

void MapCameraController::takeControlOfCamera(Camera& camera)
{
    CameraController::takeControlOfCamera(camera);
    m_targetFieldOfView = camera.fieldOfView();

    // Always look straight forward
    camera.setOrientation(quat());

    // Ensure map distance is restored
    vec3 position = camera.position();
    position.z = m_mapDistance;
    camera.setPosition(position);
}

Camera* MapCameraController::relinquishControl()
{
    return CameraController::relinquishControl();
}

void MapCameraController::update(const Input& input, float dt)
{
    ARKOSE_ASSERT(isCurrentlyControllingCamera());
    Camera& camera = *controlledCamera();

    // Apply acceleration from input

    vec3 acceleration { 0.0f };

    if (input.isKeyDown(Key::Right))
        acceleration += ark::globalRight;
    if (input.isKeyDown(Key::Left))
        acceleration -= ark::globalRight;
    if (input.isKeyDown(Key::Up))
        acceleration += ark::globalUp;
    if (input.isKeyDown(Key::Down))
        acceleration -= ark::globalUp;

    if (any(acceleration != vec3(0.0f))) {
        // Scale acceleration so that movement is less sensitive the more you're zoomed in
        static constexpr float minFovMultiplier = 0.001f;
        float fovMultiplier = minFovMultiplier + ark::inverseLerp(camera.fieldOfView(), MinFieldOfView, MaxFieldOfView) * (1.0f - minFovMultiplier);
        acceleration *= fovMultiplier;

        acceleration = normalize(acceleration) * (m_maxSpeed / TimeToMaxSpeed) * dt;
        m_velocity += acceleration;
    } else {
        // If no input and movement to acceleration decelerate instead
        if (length2(m_velocity) < StopThreshold) {
            m_velocity = vec3(0.0f);
        } else {
            vec3 deaccel = -normalize(m_velocity) * (m_maxSpeed / TimeFromMaxSpeed) * dt;
            m_velocity += deaccel;
        }
    }

    // Apply velocity to position

    float speed = length(m_velocity);
    if (speed > 0.0f) {
        speed = ark::clamp(speed, 0.0f, m_maxSpeed);
        m_velocity = normalize(m_velocity) * speed;
        camera.moveBy(m_velocity * dt);
    }

    // Apply zoom

    if (!input.isGuiUsingMouse() && !input.isKeyDown(Key::LeftShift)) {
        m_targetFieldOfView += -input.scrollDelta() * ZoomSensitivity;
        m_targetFieldOfView = ark::clamp(m_targetFieldOfView, MinFieldOfView, MaxFieldOfView);
    }
    float fov = ark::lerp(camera.fieldOfView(), m_targetFieldOfView, 1.0f - pow(0.0001f, dt));
    camera.setFieldOfView(fov);

    // Create the view matrix

    vec3 target = camera.position() + ark::globalForward;
    camera.setViewFromWorld(ark::lookAt(camera.position(), target, ark::globalUp));

    // Create the projection matrix

    // Yes, I think we do want perspective projection, even though it's a 2D map view. That's so we can have 3D objects,
    // rendered on top of the map, and also a nice relief map (i.e. height map and similar).
    camera.setProjectionFromView(ark::perspectiveProjectionToVulkanClipSpace(camera.fieldOfView(), camera.aspectRatio(), camera.nearClipPlane(), camera.farClipPlane()));

    // Finalize

    camera.finalizeModifications();
}
