#pragma once

#include "rendering/backend/Resources.h"
#include <ark/matrix.h>
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

    float customConstantBias = 0.0f;
    float customSlopeBias = 0.0f;

    virtual float constantBias(Extent2D shadowMapSize) const = 0;
    virtual float slopeBias(Extent2D shadowMapSize) const = 0;

    bool castsShadows() const { return m_castsShadows; }

    void setName(std::string name) { m_name = std::move(name); }
    const std::string& name() const { return m_name; }

private:
    Type m_type;
    bool m_castsShadows { true };
    std::string m_name {};
};
