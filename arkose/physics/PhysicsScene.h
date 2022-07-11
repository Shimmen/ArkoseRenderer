#pragma once

class PhysicsBackend;

class PhysicsScene final {
public:
    PhysicsScene(PhysicsBackend&);
    ~PhysicsScene();

    // TODO: Add interface for starting / finalizing physics frame
    //void update(float elapsedTime, float deltaTime);

    // TODO: Add interface for adding/removing physics objects (return handle)

    // TODO: Add interface for updating(?) physics objects

private:
    PhysicsBackend& m_backend;

};
