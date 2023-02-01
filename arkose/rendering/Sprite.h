#pragma once

class Camera;

#include <ark/vector.h>

struct Sprite {
    static Sprite createBillboard(Camera const&, vec3 position, vec2 size);

    vec3 points[4];
    vec3 color;

    // Will be non-null if aligned as a billboard to the camer
    Camera const* alignCamera { nullptr };
};
