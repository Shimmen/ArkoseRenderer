#include "Sprite.h"

#include "scene/camera/Camera.h"
#include <ark/vector.h>

Sprite Sprite::createBillboard(Camera const& camera, vec3 position, vec2 size)
{
    vec2 halfSize = size / 2.0f;
    vec3 p0 = position - halfSize.x * camera.right() - halfSize.y * camera.up();
    vec3 p1 = position - halfSize.x * camera.right() + halfSize.y * camera.up();
    vec3 p2 = position + halfSize.x * camera.right() + halfSize.y * camera.up();
    vec3 p3 = position + halfSize.x * camera.right() - halfSize.y * camera.up();

    return Sprite { .points = { p0, p1, p2, p3 },
                    .color = vec3(1.0f),
                    .alignCamera = &camera };
}
