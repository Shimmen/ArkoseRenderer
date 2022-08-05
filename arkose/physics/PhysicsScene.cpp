#include "PhysicsScene.h"

#include "core/Assert.h"
#include "core/Logging.h"
#include "scene/Transform.h"
#include "physics/backend/base/PhysicsBackend.h"

PhysicsScene::PhysicsScene(Scene& scene, PhysicsBackend& backend)
    : m_scene(scene)
    , m_backend(backend)
{
    m_backend.setGravity(DefaultGravity);
}

PhysicsScene::~PhysicsScene()
{
}

void PhysicsScene::setGravity(vec3 gravity)
{
    m_gravity = gravity;
    m_backend.setGravity(gravity);
}

void PhysicsScene::commitInstancesAwaitingAdd()
{
    if (m_instancesAwaitingAdd.empty()) {
        return;
    }

    // Should we always activate?
    static constexpr bool activate = true;
    m_backend.addInstanceBatchToWorld(m_instancesAwaitingAdd, activate);

    m_instancesAwaitingAdd.clear();
}

/*
PhysicsInstanceHandle PhysicsScene::createInstanceFromModel(const Model& model, MotionType motionType)
{
    SCOPED_PROFILE_ZONE_PHYSICS();

    // TODO: Reuse these model shapes!
    PhysicsShapeHandle shapeHandle = m_backend.createPhysicsShapeForModel(model);

    const Transform& transform = model.transform();
    vec3 worldPosition = transform.translation();
    quat worldOrientation = transform.orientation();

    PhysicsLayer physicsLayer {};
    bool activate = true;

    switch (motionType) {
    case MotionType::Static:
        physicsLayer = PhysicsLayer::Static;
        break;
    case MotionType::Dynamic:
    case MotionType::Kinematic:
        // TODO: Moving is not the same as movable, we could move thing between depending on the current state
        physicsLayer = PhysicsLayer::Moving;
        break;
    }

    PhysicsInstanceHandle instanceHandle = m_backend.createInstance(shapeHandle, worldPosition, worldOrientation, motionType, physicsLayer);
    m_instancesAwaitingAdd.push_back(instanceHandle);

    return instanceHandle;
}
*/

void PhysicsScene::removeInstance(PhysicsInstanceHandle instanceHandle)
{
    m_backend.removeInstanceFromWorld(instanceHandle);
}
