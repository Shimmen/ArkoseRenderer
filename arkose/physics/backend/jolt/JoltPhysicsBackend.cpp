#include "JoltPhysicsBackend.h"

#include "core/Assert.h"
#include "core/Logging.h"

#include <thread>
#include <cstdarg> // for va_list, va_start

// The Jolt headers don't include Jolt.h. Always include Jolt.h before including any other Jolt header.
#include <Jolt/Jolt.h>

// Jolt includes
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

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

static bool arkoseObjectsCanCollide(JPH::ObjectLayer objectA, JPH::ObjectLayer objectB)
{
    ARKOSE_ASSERT(objectA < NumPhysicsLayers);
    ARKOSE_ASSERT(objectB < NumPhysicsLayers);

    PhysicsLayer layerA = static_cast<PhysicsLayer>(objectA);
    PhysicsLayer layerB = static_cast<PhysicsLayer>(objectB);

    return physicsLayersCanCollide(layerA, layerB);
};

static bool arkoseBroadPhaseCanCollide(JPH::ObjectLayer objectLayer, JPH::BroadPhaseLayer broadPhaseLayer)
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
    }

    ASSERT_NOT_REACHED();
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
    m_physicsSystem->Init(cMaxBodies, cNumBodyMutexes, cMaxBodyPairs, cMaxContactConstraints, m_broadPhaseLayerInterface, arkoseBroadPhaseCanCollide, arkoseObjectsCanCollide);

    // A body activation listener gets notified when bodies activate and go to sleep. Registering one is entirely optional.
    // Note that this is called from a job so whatever you do here needs to be thread safe.
    //MyBodyActivationListener body_activation_listener;
    //physics_system.SetBodyActivationListener(&body_activation_listener);

    // A contact listener gets notified when bodies (are about to) collide, and when they separate again. Registering one is entirely optional.
    // Note that this is called from a job so whatever you do here needs to be thread safe.
    //MyContactListener contact_listener;
    //physics_system.SetContactListener(&contact_listener);

    return m_jobSystem && m_physicsSystem;
}

void JoltPhysicsBackend::shutdown()
{
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;
}
