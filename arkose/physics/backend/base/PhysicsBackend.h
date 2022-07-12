#pragma once

#include "physics/MotionType.h"
#include "physics/backend/PhysicsLayers.h"
#include "physics/HandleTypes.h"
#include <ark/vector.h>
#include <ark/quaternion.h>
#include <vector>

class Model;
class PhysicsShape;

class PhysicsBackend {
private:
    // Only one physics backend can exist at any point in time
    static PhysicsBackend* s_globalPhysicsBackend;

public:

    enum class Type {
        None,
        Jolt,
    };

    static PhysicsBackend* create(PhysicsBackend::Type);
    static void destroy();

    virtual void update(float elapsedTime, float deltaTime) = 0;

    virtual void setGravity(vec3) = 0;

    // Treat a whole model as a single rigid body (i.e. individual meshes can't move independently)
    virtual PhysicsShapeHandle createPhysicsShapeForModel(const Model&) = 0;

    virtual PhysicsInstanceHandle createInstance(PhysicsShapeHandle, vec3 position, quat orientation, MotionType, PhysicsLayer) = 0;

    virtual void addInstanceToWorld(PhysicsInstanceHandle, bool activate) = 0;
    virtual void addInstanceBatchToWorld(const std::vector<PhysicsInstanceHandle>&, bool activate) = 0;

    virtual void removeInstanceFromWorld(PhysicsInstanceHandle) = 0;
    virtual void removeInstanceBatchFromWorld(const std::vector<PhysicsInstanceHandle>&) = 0;

protected:

    virtual bool initialize() = 0;
    virtual void shutdown() = 0;

};
