#pragma once

#include "core/Assert.h"
#include "core/Types.h"
#include <type_traits>

// Layer that objects can be in, determines which other objects it can collide with
// Typically you at least want to have 1 layer for moving bodies and 1 layer for static bodies, but you can have more
// layers if you want. E.g. you could have a layer for high detail collision (which is not used by the physics simulation
// but only if you do collision testing).

enum class PhysicsLayer : uint16_t {
	Static,
	Moving,

	__Count
};

static constexpr auto NumPhysicsLayers = static_cast<std::underlying_type_t<PhysicsLayer>>(PhysicsLayer::__Count);

static constexpr bool physicsLayersCanCollide(PhysicsLayer layerA, PhysicsLayer layerB)
{
    if (layerA == PhysicsLayer::Static) {
        // Static can only collide with moving
        return layerB == PhysicsLayer::Moving;
    }

    if (layerA == PhysicsLayer::Moving) {
        // Moving can always collide with everything
        return true;
    }

    ASSERT_NOT_REACHED();
};

static constexpr const char* physicsLayerToString(PhysicsLayer physicsLayer)
{
    switch (physicsLayer) {
    case PhysicsLayer::Static:
        return "Static";
    case PhysicsLayer::Moving:
        return "Moving";
    default:
        ASSERT_NOT_REACHED();
    }
}

static constexpr std::underlying_type_t<PhysicsLayer> physicsLayerToIndex(PhysicsLayer physicsLayer)
{
    return static_cast<std::underlying_type_t<PhysicsLayer>>(physicsLayer);
}
