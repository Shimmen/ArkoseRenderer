#include "rendering/camera/CameraController.h"

#include "rendering/camera/Camera.h"

void CameraController::takeControlOfCamera(Camera& camera)
{
    m_controlledCamera = &camera;
}

Camera* CameraController::relinquishControl()
{
    Camera* camera = m_controlledCamera;
    m_controlledCamera = nullptr;
    return camera;
}
