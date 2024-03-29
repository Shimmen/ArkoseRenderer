#include "CameraController.h"

#include "scene/camera/Camera.h"

void CameraController::takeControlOfCamera(Camera& camera)
{
    m_controlledCamera = &camera;
    camera.setController({}, this);
}

Camera* CameraController::relinquishControl()
{
    Camera* camera = m_controlledCamera;
    m_controlledCamera = nullptr;

    camera->setController({}, nullptr);

    return camera;
}
