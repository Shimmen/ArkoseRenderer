#pragma once

#include "physics/backend/PhysicsLayers.h"
#include "physics/backend/base/PhysicsBackend.h"

#include <memory>

#include <Jolt/Jolt.h>
#include <Jolt/Core/JobSystem.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/PhysicsSystem.h>

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

private:
    std::unique_ptr<JPH::PhysicsSystem> m_physicsSystem {};
    std::unique_ptr<JPH::TempAllocator> m_tempAllocator {};
    std::unique_ptr<JPH::JobSystem> m_jobSystem {};

    ArkoseBroadPhaseLayerInterface m_broadPhaseLayerInterface {};

    // We simulate the physics world in discrete time steps. 60 Hz is a good rate to update the physics system.
    static constexpr float FixedUpdateRate { 1.0f / 60.0f };
    float m_fixedRateAccumulation { 0.0f };
};
