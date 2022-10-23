#pragma once

#include "physics/backend/PhysicsLayers.h"
#include "physics/backend/base/PhysicsBackend.h"

#include <memory>

#include <Jolt/Jolt.h>
#include <Jolt/Core/JobSystem.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/PhysicsSystem.h>

class JoltVisualiser;

class ArkoseBroadPhaseLayerInterface final : public JPH::BroadPhaseLayerInterface {
public:
    ArkoseBroadPhaseLayerInterface();

    virtual JPH::uint GetNumBroadPhaseLayers() const override;
    virtual JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer) const override;

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    virtual const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override;
#endif // JPH_EXTERNAL_PROFILE || JPH_PROFILE_ENABLED

private:
    JPH::BroadPhaseLayer m_objectToBroadPhase[NumPhysicsLayers];
};

class JoltPhysicsBackend : public PhysicsBackend {
public:

    JoltPhysicsBackend();
    virtual ~JoltPhysicsBackend();

    virtual bool initialize() override;
    virtual void shutdown() override;

    virtual void update(float elapsedTime, float deltaTime) override;
    void fixedRateUpdate(float fixedRate, int numCollisionSteps);

    virtual void setGravity(vec3) override;

    virtual PhysicsShapeHandle createPhysicsShapeForBox(vec3 halfExtent) override;
    virtual PhysicsShapeHandle createPhysicsShapeForTriangleMesh(PhysicsMesh const&) override;
    virtual PhysicsShapeHandle createPhysicsShapeForTriangleMeshes(std::vector<PhysicsMesh> const&) override;

    virtual PhysicsInstanceHandle createInstance(PhysicsShapeHandle, vec3 position, quat orientation, MotionType, PhysicsLayer) override;
    virtual void attachRenderTransform(PhysicsInstanceHandle, Transform*) override;

    virtual void addInstanceToWorld(PhysicsInstanceHandle, bool activate) override;
    virtual void addInstanceBatchToWorld(const std::vector<PhysicsInstanceHandle>&, bool activate) override;

    virtual void removeInstanceFromWorld(PhysicsInstanceHandle) override;
    virtual void removeInstanceBatchFromWorld(const std::vector<PhysicsInstanceHandle>&) override;

    virtual void applyImpulse(PhysicsInstanceHandle, vec3 impulse) override;

private:

    JPH::ObjectLayer physicsLayerToJoltObjectLayer(PhysicsLayer) const;
    JPH::EMotionType motionTypeToJoltMotionType(MotionType) const;

    PhysicsShapeHandle registerShape(JPH::ShapeRefC&&);

    JPH::BodyID getBodyId(PhysicsInstanceHandle) const;

    void updateRenderDataForNonStaticInstances(float alpha);

    std::unique_ptr<JPH::PhysicsSystem> m_physicsSystem {};
    std::unique_ptr<JPH::TempAllocator> m_tempAllocator {};
    std::unique_ptr<JPH::JobSystem> m_jobSystem {};

#if JPH_DEBUG_RENDERER
    std::unique_ptr<JoltVisualiser> m_visualiser {};
#endif

    ArkoseBroadPhaseLayerInterface m_broadPhaseLayerInterface {};

    // We simulate the physics world in discrete time steps. 60 Hz is a good rate to update the physics system.
    static constexpr float FixedUpdateRate { 1.0f / 60.0f };
    float m_fixedRateAccumulation { 0.0f };

    std::vector<JPH::ShapeRefC> m_shapes {};
    std::vector<size_t> m_shapesFreeList {};

    // TODO: Keep a free list!
    std::vector<JPH::BodyID> m_bodyInstances {};

    // TODO: Maybe just make this a vector of bodyID-Transform* pairs?
    std::unordered_map<JPH::BodyID, Transform*> m_bodyIdToRenderTransformMap {};
};
