#include "JoltPhysicsBackend.h"

#include "core/Assert.h"
#include "core/Logging.h"
#include "physics/backend/PhysicsLayers.h"
#include "physics/backend/jolt/JoltVisualiser.h"
#include "scene/Transform.h"
#include "utility/Profiling.h"

#include <cstdarg> // for va_list, va_start

// The Jolt headers don't include Jolt.h. Always include Jolt.h before including any other Jolt header.
#include <Jolt/Jolt.h>

// Jolt includes
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/PhysicsMaterialSimple.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

// Jolt serialization
//#include <Jolt/ObjectStream/ObjectStreamBinaryIn.h>
#include <Jolt/ObjectStream/ObjectStreamBinaryOut.h>
//#include <Jolt/ObjectStream/ObjectStreamTextOut.h>

// Disable common warnings triggered by Jolt, you can use JPH_SUPPRESS_WARNING_PUSH / JPH_SUPPRESS_WARNING_POP to store and restore the warning state
JPH_SUPPRESS_WARNINGS

// Callback for traces, connect this to your own trace function if you have one
static void ArkoseJoltPhysicsTraceImpl(const char* format, ...)
{
    va_list list;
    va_start(list, format);

    char buffer[4096];
    vsnprintf(buffer, sizeof(buffer), format, list);

    ARKOSE_LOG(Info, "{}", buffer);
}

#ifdef JPH_ENABLE_ASSERTS
static bool ArkoseJoltAssertFailedImpl(const char* inExpression, const char* inMessage, const char* inFile, JPH::uint inLine)
{
    // Print to the TTY
    ARKOSE_LOG(Error, "{}:{}: ({}) {}", inFile, inLine, inExpression, (inMessage != nullptr ? inMessage : ""));

    // Breakpoint
    return true;
};
#endif // JPH_ENABLE_ASSERTS


// Each broadphase layer results in a separate bounding volume tree in the broad phase. You at least want to have
// a layer for non-moving and moving objects to avoid having to update a tree full of static objects every frame.
// You can have a 1-on-1 mapping between object layers and broadphase layers (like in this case) but if you have
// many object layers you'll be creating many broad phase trees, which is not efficient. If you want to fine tune
// your broadphase layers define JPH_TRACK_BROADPHASE_STATS and look at the stats reported on the TTY.
namespace ArkoseBroadPhaseLayers {
static constexpr JPH::BroadPhaseLayer Static { 0 };
static constexpr JPH::BroadPhaseLayer Moving { 1 };
static constexpr JPH::uint Count { 2 };
};

ArkoseBroadPhaseLayerInterface::ArkoseBroadPhaseLayerInterface()
{
    // Create a mapping table from object to broad phase layer
    m_objectToBroadPhase[physicsLayerToIndex(PhysicsLayer::Static)] = ArkoseBroadPhaseLayers::Static;
    m_objectToBroadPhase[physicsLayerToIndex(PhysicsLayer::Moving)] = ArkoseBroadPhaseLayers::Moving;
}

JPH::uint ArkoseBroadPhaseLayerInterface::GetNumBroadPhaseLayers() const
{
    return ArkoseBroadPhaseLayers::Count;
}

JPH::BroadPhaseLayer ArkoseBroadPhaseLayerInterface::GetBroadPhaseLayer(JPH::ObjectLayer layer) const
{
    ARKOSE_ASSERT(layer < NumPhysicsLayers);
    return m_objectToBroadPhase[layer];
}

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
const char* ArkoseBroadPhaseLayerInterface::GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const
{
    switch ((JPH::BroadPhaseLayer::Type)layer) {
    case (JPH::BroadPhaseLayer::Type)ArkoseBroadPhaseLayers::Static:
        return "Static";
    case (JPH::BroadPhaseLayer::Type)ArkoseBroadPhaseLayers::Moving:
        return "Moving";
    default:
        ASSERT_NOT_REACHED();
    }
}
#endif // JPH_EXTERNAL_PROFILE || JPH_PROFILE_ENABLED

bool ArkoseObjectLayerPairFilter::ShouldCollide(JPH::ObjectLayer objectA, JPH::ObjectLayer objectB) const
{
    ARKOSE_ASSERT(objectA < NumPhysicsLayers);
    ARKOSE_ASSERT(objectB < NumPhysicsLayers);

    PhysicsLayer layerA = static_cast<PhysicsLayer>(objectA);
    PhysicsLayer layerB = static_cast<PhysicsLayer>(objectB);

    return physicsLayersCanCollide(layerA, layerB);
}

bool ArkoseObjectVsBroadPhaseLayerFilter::ShouldCollide(JPH::ObjectLayer objectLayer, JPH::BroadPhaseLayer broadPhaseLayer) const
{
    ARKOSE_ASSERT(objectLayer < NumPhysicsLayers);
    PhysicsLayer objectPhysicsLayer = static_cast<PhysicsLayer>(objectLayer);

    switch (objectPhysicsLayer) {
    case PhysicsLayer::Static:
        // Static objects can only collide with objects in the moving broad phase
        return broadPhaseLayer == ArkoseBroadPhaseLayers::Moving;
    case PhysicsLayer::Moving:
        // Moving objects can always collide with all the broad phases
        return true;
    default:
        ASSERT_NOT_REACHED();
    }
}

JoltPhysicsBackend::JoltPhysicsBackend()
{
}

JoltPhysicsBackend::~JoltPhysicsBackend()
{
}

bool JoltPhysicsBackend::initialize()
{
    // NOTE: Based on JoltPhysics hello world sample

    // Register allocation hook
    JPH::RegisterDefaultAllocator();

    // Install callbacks
    JPH::Trace = ArkoseJoltPhysicsTraceImpl;
    JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = ArkoseJoltAssertFailedImpl;)

    // We need a temp allocator for temporary allocations during the physics update. We're pre-allocating 10 MB to avoid having to do allocations
    // during the physics update. If you don't want to pre-allocate you can also use TempAllocatorMalloc to fall back to malloc / free.
    m_tempAllocator = std::make_unique<JPH::TempAllocatorImpl>(10 * 1024 * 1024);

    // Create a factory
    JPH::Factory::sInstance = new JPH::Factory();

    // Register all Jolt physics types
    JPH::RegisterTypes();

    // We need a job system that will execute physics jobs on multiple threads. Typically you would implement the JobSystem interface yourself
    // and let Jolt Physics run on top of your own job scheduler. JobSystemThreadPool is an example implementation.
    // TODO: Replace with the Arkose task graph once it's mature enough or at least proven to work for this
    constexpr int numPhysicsJobThreads = 4;
    m_jobSystem = std::make_unique<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, numPhysicsJobThreads);

    // This is the max amount of rigid bodies that you can add to the physics system. If you try to add more you'll get an error.
    // Note: For a real project use something in the order of 65536.
    const JPH::uint cMaxBodies = 65536;

    // This determines how many mutexes to allocate to protect rigid bodies from concurrent access. Set it to 0 for the default settings.
    const JPH::uint cNumBodyMutexes = 0;

    // This is the max amount of body pairs that can be queued at any time (the broad phase will detect overlapping
    // body pairs based on their bounding boxes and will insert them into a queue for the narrowphase). If you make this buffer
    // too small the queue will fill up and the broad phase jobs will start to do narrow phase work. This is slightly less efficient.
    // Note: For a real project use something in the order of 65536.
    const JPH::uint cMaxBodyPairs = 65536;

    // This is the maximum size of the contact constraint buffer. If more contacts (collisions between bodies) are detected than this
    // number then these contacts will be ignored and bodies will start interpenetrating / fall through the world.
    // Note: For a real project use something in the order of 10240.
    const JPH::uint cMaxContactConstraints = 10240;

    // Create the actual physics system.
    m_physicsSystem = std::make_unique<JPH::PhysicsSystem>();
    m_physicsSystem->Init(cMaxBodies, cNumBodyMutexes, cMaxBodyPairs, cMaxContactConstraints, m_broadPhaseLayerInterface, m_objectVsBroadPhaseLayerFilter, m_objectLayerPairFilter);

    // A body activation listener gets notified when bodies activate and go to sleep. Registering one is entirely optional.
    // Note that this is called from a job so whatever you do here needs to be thread safe.
    //MyBodyActivationListener body_activation_listener;
    //physics_system.SetBodyActivationListener(&body_activation_listener);

    // A contact listener gets notified when bodies (are about to) collide, and when they separate again. Registering one is entirely optional.
    // Note that this is called from a job so whatever you do here needs to be thread safe.
    //MyContactListener contact_listener;
    //physics_system.SetContactListener(&contact_listener);

#if JPH_DEBUG_RENDERER
    m_visualiser = std::make_unique<JoltVisualiser>();
#endif

    return m_jobSystem && m_physicsSystem;
}

void JoltPhysicsBackend::shutdown()
{
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;
}

void JoltPhysicsBackend::update(float elapsedTime, float deltaTime)
{
    SCOPED_PROFILE_ZONE_PHYSICS();

    ARKOSE_ASSERT(deltaTime >= 1e-6f);
    m_fixedRateAccumulation += deltaTime;

    // If you take larger steps than 1 / 60th of a second you need to do multiple collision steps in order to keep the simulation stable.
    // Do 1 collision step per 1 / 60th of a second (round up). Also avoid taking too many steps, in case we get a lag spike.
    float numCollisionSteps = std::min(std::ceil(m_fixedRateAccumulation / FixedUpdateRate), 25.0f);

    if (numCollisionSteps >= 1.0f) {

        float timeToStep = numCollisionSteps * FixedUpdateRate;
        fixedRateUpdate(timeToStep, static_cast<int>(numCollisionSteps));

        m_fixedRateAccumulation -= timeToStep;

    } else {
        // We have some time now, why not do some optimization?
        //if (m_numIndividualBodiesAddedSinceLastOptimize > 0) {
        //    SCOPED_PROFILE_ZONE_PHYSICS_NAMED("Optimize Broad Phase");
        //    m_numIndividualBodiesAddedSinceLastOptimize = 0;
        //    m_physicsSystem->OptimizeBroadPhase();
        //}
    }

    // See https://gafferongames.com/post/fix_your_timestep/
    float alpha = m_fixedRateAccumulation / FixedUpdateRate;
    updateRenderDataForNonStaticInstances(alpha);

#if JPH_DEBUG_RENDERER
    if (m_visualiser != nullptr) {
        //JPH::BodyManager::DrawSettings drawSettings {};
        //m_physicsSystem->DrawBodies(drawSettings, m_visualiser.get());
        //m_physicsSystem->DrawConstraints(m_visualiser.get());
        //m_physicsSystem->DrawConstraintLimits(m_visualiser.get());
        //m_physicsSystem->DrawConstraintReferenceFrame(m_visualiser.get());
    }
#endif
}

void JoltPhysicsBackend::fixedRateUpdate(float fixedRate, int numCollisionSteps)
{
    SCOPED_PROFILE_ZONE_PHYSICS();

    ARKOSE_ASSERT(numCollisionSteps >= 1);

    m_physicsSystem->Update(fixedRate, numCollisionSteps, m_tempAllocator.get(), m_jobSystem.get());
}

void JoltPhysicsBackend::setGravity(vec3 gravity)
{
    auto joltGravity = JPH::Vec3(gravity.x, gravity.y, gravity.z);
    m_physicsSystem->SetGravity(joltGravity);
}

PhysicsShapeHandle JoltPhysicsBackend::createPhysicsShapeForBox(vec3 halfExtent)
{
    JPH::Vec3 joltHalfExtent = JPH::Vec3(halfExtent.x, halfExtent.y, halfExtent.z);
    auto boxShape = JPH::ShapeRefC(new JPH::BoxShape(joltHalfExtent));
    return registerShape(std::move(boxShape));
}

PhysicsShapeHandle JoltPhysicsBackend::createPhysicsShapeForTriangleMesh(PhysicsMesh const& mesh)
{
    return createPhysicsShapeForTriangleMeshes({ mesh });
}

PhysicsShapeHandle JoltPhysicsBackend::createPhysicsShapeForTriangleMeshes(std::vector<PhysicsMesh> const& meshes)
{
    SCOPED_PROFILE_ZONE_PHYSICS();

    JPH::VertexList vertices {}; // OPTIMIZATION: Can we avoid copying vertex data? JPH::VertexList is just a std::vector with a 3xFloat struct inside
    JPH::IndexedTriangleList indexedTriangles {};
    JPH::PhysicsMaterialList physicsMaterials {};

    uint32_t indexOffset = 0;
    for (PhysicsMesh const& mesh : meshes) {

        // TODO: Use the physics materials from the PhysicsMesh!
        constexpr uint32_t physicsMaterialIdx = 0;

        for (vec3 const& position : mesh.positions) {
            vertices.emplace_back(position.x, position.y, position.z);
        }

        ARKOSE_ASSERT(mesh.indices.size() % 3 == 0);
        size_t numTriangles = mesh.indices.size() / 3;

        for (size_t triangleIdx = 0; triangleIdx < numTriangles; ++triangleIdx) {
            uint32_t i0 = mesh.indices[3 * triangleIdx + 0] + indexOffset;
            uint32_t i1 = mesh.indices[3 * triangleIdx + 1] + indexOffset;
            uint32_t i2 = mesh.indices[3 * triangleIdx + 2] + indexOffset;
            indexedTriangles.emplace_back(i0, i1, i2, physicsMaterialIdx);
        }

        ARKOSE_ASSERT(vertices.size() <= UINT32_MAX);
        indexOffset = static_cast<uint32_t>(vertices.size());
    }

    JPH::MeshShapeSettings meshShapeSettings { vertices, indexedTriangles, physicsMaterials };

    JPH::ShapeSettings::ShapeResult meshShapeResult;
    {
        SCOPED_PROFILE_ZONE_PHYSICS_NAMED("Create mesh shape");
        meshShapeResult = meshShapeSettings.Create();
    }

    if (meshShapeResult.HasError()) {
        ARKOSE_LOG(Error, "JoltPhysics error trying to create mesh shape: {}", meshShapeResult.GetError());
    }

    if (JPH::ShapeRefC shapeRef = meshShapeResult.Get()) {
        return registerShape(std::move(shapeRef));
    } else {
        return PhysicsShapeHandle();
    }
}

PhysicsInstanceHandle JoltPhysicsBackend::createInstance(PhysicsShapeHandle shapeHandle, vec3 position, quat orientation, MotionType motionType, PhysicsLayer physicsLayer)
{
    SCOPED_PROFILE_ZONE_PHYSICS();

    ARKOSE_ASSERT(shapeHandle.valid());
    JPH::ShapeRefC shapeRef = m_shapes[shapeHandle.index()];
    ARKOSE_ASSERT(shapeRef.GetPtr());

    // TODO: object layer could/should(?) be deduced from motion type?
    JPH::EMotionType joltMotionType = motionTypeToJoltMotionType(motionType);
    JPH::ObjectLayer objectLayer = physicsLayerToJoltObjectLayer(physicsLayer);

    // Create the settings for the body itself. Note that here you can also set other properties like the restitution / friction.

    auto joltPosition = JPH::Vec3(position.x, position.y, position.z);
    auto joltOrientation = JPH::Quat(orientation.vec.x, orientation.vec.y, orientation.vec.z, orientation.w).Normalized();
    JPH::BodyCreationSettings bodyCreationSettings { shapeRef, joltPosition, joltOrientation, joltMotionType, objectLayer };

    JPH::BodyInterface& bodyInterface = m_physicsSystem->GetBodyInterface();
    JPH::Body* body = bodyInterface.CreateBody(bodyCreationSettings);

    if (body == nullptr) {
        ARKOSE_LOG(Error, "JoltPhysics: failed to create body since we've run out.");
        return PhysicsInstanceHandle();
    } else {
        uint64_t index = m_bodyInstances.size();
        m_bodyInstances.push_back(body->GetID());
        return PhysicsInstanceHandle(index);
    }
}

void JoltPhysicsBackend::attachRenderTransform(PhysicsInstanceHandle instanceHandle, Transform* renderTransform)
{
    m_bodyIdToRenderTransformMap[getBodyId(instanceHandle)] = renderTransform;
}

void JoltPhysicsBackend::addInstanceToWorld(PhysicsInstanceHandle instanceHandle, bool activate)
{
    JPH::BodyInterface& bodyInterface = m_physicsSystem->GetBodyInterface();
    JPH::EActivation activation = (activate) ? JPH::EActivation::Activate : JPH::EActivation::DontActivate;
    bodyInterface.AddBody(getBodyId(instanceHandle), activation);
}

void JoltPhysicsBackend::addInstanceBatchToWorld(const std::vector<PhysicsInstanceHandle>& instanceHandles, bool activate)
{
    std::vector<JPH::BodyID> bodyIDs {};
    bodyIDs.reserve(instanceHandles.size());
    for (PhysicsInstanceHandle handle : instanceHandles) {
        bodyIDs.push_back(getBodyId(handle));
    }

    JPH::BodyInterface& bodyInterface = m_physicsSystem->GetBodyInterface();

    JPH::EActivation activation = (activate) ? JPH::EActivation::Activate : JPH::EActivation::DontActivate;
    
    JPH::BodyID* bodyIDsPtr = bodyIDs.data();
    int numBodies = static_cast<int>(bodyIDs.size());

    // TODO: This can be nicely multithreaded!
    JPH::BodyInterface::AddState addState = bodyInterface.AddBodiesPrepare(bodyIDsPtr, numBodies);
    bodyInterface.AddBodiesFinalize(bodyIDsPtr, numBodies, addState, activation);

    // We probably shouldn't do that here..
    //m_physicsSystem->OptimizeBroadPhase();
}

void JoltPhysicsBackend::removeInstanceFromWorld(PhysicsInstanceHandle instanceHandle)
{
    JPH::BodyInterface& bodyInterface = m_physicsSystem->GetBodyInterface();
    bodyInterface.RemoveBody(getBodyId(instanceHandle));
}

void JoltPhysicsBackend::removeInstanceBatchFromWorld(const std::vector<PhysicsInstanceHandle>& instanceHandles)
{
    std::vector<JPH::BodyID> bodyIDs {};
    bodyIDs.reserve(instanceHandles.size());
    for (PhysicsInstanceHandle handle : instanceHandles) {
        bodyIDs.push_back(getBodyId(handle));
    }

    JPH::BodyInterface& bodyInterface = m_physicsSystem->GetBodyInterface();
    bodyInterface.RemoveBodies(bodyIDs.data(), static_cast<int>(bodyIDs.size()));
}

void JoltPhysicsBackend::applyImpulse(PhysicsInstanceHandle instanceHandle, vec3 impulse)
{
    JPH::BodyInterface& bodyInterface = m_physicsSystem->GetBodyInterface();
    bodyInterface.AddImpulse(getBodyId(instanceHandle), JPH::Vec3Arg(impulse.x, impulse.y, impulse.z));
}

JPH::ObjectLayer JoltPhysicsBackend::physicsLayerToJoltObjectLayer(PhysicsLayer physicsLayer) const
{
    auto index = physicsLayerToIndex(physicsLayer);
    return static_cast<JPH::ObjectLayer>(index);
}

JPH::EMotionType JoltPhysicsBackend::motionTypeToJoltMotionType(MotionType motionType) const
{
    switch (motionType) {
    case MotionType::Static:
        return JPH::EMotionType::Static;
    case MotionType::Kinematic:
        return JPH::EMotionType::Kinematic;
    case MotionType::Dynamic:
        return JPH::EMotionType::Dynamic;
    default:
        ASSERT_NOT_REACHED();
    }
}

PhysicsShapeHandle JoltPhysicsBackend::registerShape(JPH::ShapeRefC&& shapeRef)
{
    if (m_shapesFreeList.empty()) {
        m_shapes.emplace_back(std::move(shapeRef));
        return PhysicsShapeHandle(m_shapes.size() - 1);
    } else {
        size_t idx = m_shapesFreeList.back();
        m_shapesFreeList.pop_back();
        m_shapes[idx] = std::move(shapeRef);
        return PhysicsShapeHandle(idx);
    }
}

JPH::BodyID JoltPhysicsBackend::getBodyId(PhysicsInstanceHandle instanceHandle) const
{
    ARKOSE_ASSERT(instanceHandle.valid());
    JPH::BodyID bodyId = m_bodyInstances[instanceHandle.index()];
    ARKOSE_ASSERT(not bodyId.IsInvalid());
    return bodyId;
}

void JoltPhysicsBackend::updateRenderDataForNonStaticInstances(float alpha)
{
    JPH::BodyInterface const& bodyInterface = m_physicsSystem->GetBodyInterface();

    for (auto& [bodyId, renderTransform] : m_bodyIdToRenderTransformMap) {
        ARKOSE_ASSERT(renderTransform);

        JPH::Vec3 joltPosition;
        JPH::Quat joltOrientation;
        bodyInterface.GetPositionAndRotation(bodyId, joltPosition, joltOrientation);

        renderTransform->setPositionInWorld({ joltPosition.GetX(), joltPosition.GetY(), joltPosition.GetZ() });
        renderTransform->setOrientationInWorld({ { joltOrientation.GetX(), joltOrientation.GetY(), joltOrientation.GetZ() }, joltOrientation.GetW() });
    }
}

void JoltPhysicsBackend::createPhysicsShapeForExport(PhysicsMesh const& mesh) const
{
    SCOPED_PROFILE_ZONE_PHYSICS();

    // TODO: Use the physics materials from the PhysicsMesh!
    constexpr u32 physicsMaterialIdx = 0;

    JPH::VertexList vertices {}; // OPTIMIZATION: Can we avoid copying vertex data? JPH::VertexList is just a std::vector with a 3xFloat struct inside
    JPH::IndexedTriangleList indexedTriangles {};
    JPH::PhysicsMaterialList physicsMaterials {};

    for (vec3 const& position : mesh.positions) {
        vertices.emplace_back(position.x, position.y, position.z);
    }

    ARKOSE_ASSERT(mesh.indices.size() % 3 == 0);
    size_t numTriangles = mesh.indices.size() / 3;

    for (size_t triangleIdx = 0; triangleIdx < numTriangles; ++triangleIdx) {
        uint32_t i0 = mesh.indices[3 * triangleIdx + 0];
        uint32_t i1 = mesh.indices[3 * triangleIdx + 1];
        uint32_t i2 = mesh.indices[3 * triangleIdx + 2];
        indexedTriangles.emplace_back(i0, i1, i2, physicsMaterialIdx);
    }

    JPH::MeshShapeSettings meshShapeSettings = JPH::MeshShapeSettings(vertices, indexedTriangles, physicsMaterials);

    JPH::ShapeSettings::ShapeResult meshShapeResult;
    {
        SCOPED_PROFILE_ZONE_PHYSICS_NAMED("Create mesh shape");
        meshShapeResult = meshShapeSettings.Create();
    }

    if (meshShapeResult.HasError()) {
        ARKOSE_LOG(Error, "JoltPhysics error trying to create mesh shape: {}", meshShapeResult.GetError());
    }

    JPH::ShapeRefC shapeRef = meshShapeResult.Get();

    // WIP stuff...

    //std::ofstream outStream;
    //outStream.open("assets/physics-data.bin");
    //JPH::ObjectStreamBinaryOut joltOutStream{outStream};
    //shapeRef->SaveBinaryState(joltOutStream);
}
