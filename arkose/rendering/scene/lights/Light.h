#pragma once

#include "backend/Resources.h"
#include <moos/matrix.h>
#include <fmt/format.h>

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
        static int nextLightId = 0;
        m_name = fmt::format("light-{}", nextLightId++);
    }

    virtual ~Light() { }

    // Linear sRGB color
    vec3 color { 1.0f };

    Type type() const { return m_type; }

    virtual vec3 position() const { return vec3(); };
    virtual float intensityValue() const = 0;
    virtual vec3 forwardDirection() const = 0;

    virtual mat4 lightViewMatrix() const = 0;
    virtual mat4 projectionMatrix() const = 0;
    mat4 viewProjection() const { return projectionMatrix() * lightViewMatrix(); };

    Extent2D shadowMapSize() const { return m_shadowMapSize; }
    void setShadowMapSize(Extent2D size);

    float customConstantBias = 0.0f;
    float customSlopeBias = 0.0f;

    virtual float constantBias() = 0;
    virtual float slopeBias() = 0;

    bool castsShadows() const { return m_castsShadows; }

    Texture& shadowMap();
    RenderTarget& shadowMapRenderTarget();

    // TODO: Remove this API and replace it with something less stupid.
    RenderState& getOrCreateCachedShadowMapRenderState(const std::string& cacheIdentifier, std::function<RenderState&()> creationCallback);
    void invalidateRenderStateCache();

    void setName(std::string name) { m_name = std::move(name); }
    const std::string& name() const { return m_name; }

private:
    Type m_type;

    bool m_castsShadows { true };
    Extent2D m_shadowMapSize { 1024u, 1024u };
    std::unique_ptr<Texture> m_shadowMap { nullptr };
    std::unique_ptr<RenderTarget> m_shadowMapRenderTarget { nullptr };

    // NOTE: Uses raw ptr since the registry owns the resource
    std::unordered_map<std::string, RenderState*> m_cachedRenderStates {};

    std::string m_name {};
};
