#pragma once

#include "backend/Resources.h"
#include "utility/Badge.h"
#include <moos/matrix.h>

class Scene;

class Light {
public:
    explicit Light(vec3 color)
        : color(color)
    {
    }

    // Linear sRGB color
    vec3 color { 1.0f };

    virtual mat4 viewProjection() const = 0;

    Extent2D shadowMapSize() const { return m_shadowMapSize; }
    void setShadowMapSize(Extent2D size);

    Texture& shadowMap();

    void setScene(Badge<Scene>, Scene* scene) { m_scene = scene; }
    const Scene* scene() const { return m_scene; }
    Scene* scene() { return m_scene; }

private:
    Scene* m_scene { nullptr };

    bool m_castsShadows { true };
    Extent2D m_shadowMapSize { 1024u, 1024u };
    Texture* m_shadowMap { nullptr };
};
