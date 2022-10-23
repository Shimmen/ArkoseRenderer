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

PhysicsInstanceHandle PhysicsScene::createStaticInstance(PhysicsShapeHandle shapeHandle, Transform staticTransform)
{
    SCOPED_PROFILE_ZONE_PHYSICS();

    vec3 worldPosition = staticTransform.positionInWorld();
    quat worldOrientation = staticTransform.orientationInWorld();

    PhysicsInstanceHandle instanceHandle = m_backend.createInstance(shapeHandle, worldPosition, worldOrientation, MotionType::Static, PhysicsLayer::Static);
    m_instancesAwaitingAdd.push_back(instanceHandle);

    return instanceHandle;
}

PhysicsInstanceHandle PhysicsScene::createDynamicInstance(PhysicsShapeHandle shapeHandle, Transform& renderTransform)
{
    SCOPED_PROFILE_ZONE_PHYSICS();

    vec3 worldPosition = renderTransform.positionInWorld();
    quat worldOrientation = renderTransform.orientationInWorld();

    PhysicsInstanceHandle instanceHandle = m_backend.createInstance(shapeHandle, worldPosition, worldOrientation, MotionType::Dynamic, PhysicsLayer::Moving);

    // NOTE: Deferred batch add doesn't work if we e.g. want to spawn and immediately apply forces to it, so let's not do it for dynamic instances.
    m_backend.addInstanceToWorld(instanceHandle, true);
    m_backend.attachRenderTransform(instanceHandle, &renderTransform);

    return instanceHandle;
}

void PhysicsScene::removeInstance(PhysicsInstanceHandle instanceHandle)
{
    m_backend.removeInstanceFromWorld(instanceHandle);
}
