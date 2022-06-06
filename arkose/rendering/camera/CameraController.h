#pragma once

class Camera;
class Input;

class CameraController {
public:
    CameraController() = default;
    ~CameraController() = default;

	virtual void takeControlOfCamera(Camera&);
    virtual Camera* relinquishControl();

    bool isCurrentlyControllingCamera() const { return m_controlledCamera != nullptr; }
    const Camera* controlledCamera() const { return m_controlledCamera; }
    Camera* controlledCamera() { return m_controlledCamera; }

	virtual void update(const Input&, float deltaTime) = 0;

    virtual void setTargetFocusDepth(float) {};
    virtual void clearTargetFocusDepth() {};

private:
    Camera* m_controlledCamera { nullptr };
	
};