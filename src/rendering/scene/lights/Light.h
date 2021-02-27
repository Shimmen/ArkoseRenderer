#pragma once

#include "backend/Resources.h"
#include "utility/Badge.h"
#include <moos/matrix.h>

class Scene;


class Light {
public:

    enum class Type {
        DirectionalLight,
        PointLight,
        SpotLight,
    };

    explicit Light(Type type, vec3 color)
        : color(color)
        , m_type(type)
    {
    }

    virtual ~Light() { }

    // Linear sRGB color
    vec3 color { 1.0f };

    Type type() const { return m_type; }

    virtual vec3 position() const { return vec3(); };
    virtual float intensityValue() const = 0;
    virtual vec3 forwardDirection() const = 0;
    virtual mat4 viewProjection() const = 0;

    Extent2D shadowMapSize() const { return m_shadowMapSize; }
    void setShadowMapSize(Extent2D size);

    bool castsShadows() const { return m_castsShadows; }

    Texture& shadowMap();
    RenderTarget& shadowMapRenderTarget();
    RenderState& getOrCreateCachedShadowMapRenderState(const std::string& cacheIdentifier, std::function<RenderState&(Registry& sceneRegistry)> creationCallback);
    

    void setScene(Badge<Scene>, Scene* scene) { m_scene = scene; }
    const Scene* scene() const { return m_scene; }
    Scene* scene() { return m_scene; }

private:
    Scene* m_scene { nullptr };

    Type m_type;

    bool m_castsShadows { true };
    Extent2D m_shadowMapSize { 1024u, 1024u };
    Texture* m_shadowMap { nullptr };
    RenderTarget* m_shadowMapRenderTarget { nullptr };
    std::unordered_map<std::string, RenderState*> m_cachedRenderStates {};
};
