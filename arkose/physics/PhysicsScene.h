#pragma once

#include "physics/HandleTypes.h"
#include "physics/MotionType.h"
#include "rendering/scene/Model.h"

class Transform;
class Scene;
class PhysicsBackend;

class PhysicsScene final {
public:
    PhysicsScene(Scene&, PhysicsBackend&);
    ~PhysicsScene();

    Scene& scene() { return m_scene; }
    const Scene& scene() const { return m_scene; }

    void setGravity(vec3);
    vec3 gravity() const { return m_gravity; }

    void commitInstancesAwaitingAdd();

    // Can we keep shapes and such at this abstraction level? Would be a nice interface from the Scene -> PhysisScene.
    // For physics shapes that have a simpler version than the actual triangle mesh that info is kept inside the Model
    // and when we create an instance from a model we will register both shapes, one for simple and one for complex.

    PhysicsInstanceHandle createInstanceFromModel(const Model&, MotionType);
    void removeInstance(PhysicsInstanceHandle);

    // TODO: Add interface for updating(?) physics objects

private:
    Scene& m_scene;
    PhysicsBackend& m_backend;

    static constexpr vec3 DefaultGravity = vec3(0.0f, -9.81f, 0.0f);
    vec3 m_gravity { DefaultGravity };

    // Prefer batch adding for the sake of broad phase performace
    std::vector<PhysicsInstanceHandle> m_instancesAwaitingAdd {};

};