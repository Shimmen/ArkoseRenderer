#pragma once

class Scene;
class PhysicsBackend;

class PhysicsScene final {
public:
    PhysicsScene(Scene&, PhysicsBackend&);
    ~PhysicsScene();

    Scene& scene() { return m_scene; }
    const Scene& scene() const { return m_scene; }

    // TODO: Add interface for starting / finalizing physics frame
    //void update(float elapsedTime, float deltaTime);

    // TODO: Add interface for adding/removing physics objects (return handle)

    // TODO: Add interface for updating(?) physics objects

private:
    Scene& m_scene;
    PhysicsBackend& m_backend;

};
