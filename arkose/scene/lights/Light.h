#pragma once

#include "rendering/backend/Resources.h"
#include "scene/editor/EditorObject.h"
#include <ark/matrix.h>

class Light : public IEditorObject {
public:

    enum class Type {
        DirectionalLight,
        SphereLight,
        SpotLight,
    };

    Light() = default;
    Light(Type type, vec3 color);

    virtual ~Light() { }

    vec3 color() const { return m_color; }
    vec3 setColor(vec3 color) { m_color = color; }

    Type type() const { return m_type; }

    // ITransformable interface
    Transform& transform() override { return m_transform; }
    Transform const& transform() const { return m_transform; }

    // IEditorObject interface
    virtual bool shouldDrawGui() const override;
    virtual void drawGui() override;

    // Direction of outgoing light, i.e. -L in a BRDF
    virtual vec3 forwardDirection() const;

    virtual mat4 lightViewMatrix() const;

    virtual float intensityValue() const = 0;
    virtual mat4 projectionMatrix() const = 0;

    mat4 viewProjection() const { return projectionMatrix() * lightViewMatrix(); };

    float customConstantBias = 0.0f;
    float customSlopeBias = 0.0f;

    virtual float constantBias(Extent2D shadowMapSize) const = 0;
    virtual float slopeBias(Extent2D shadowMapSize) const = 0;

    bool castsShadows() const { return m_castsShadows; }

    void setName(std::string name) { m_name = std::move(name); }
    const std::string& name() const { return m_name; }

    template<class Archive>
    void serialize(Archive&);

private:
    Type m_type { Type::DirectionalLight };
    bool m_castsShadows { true };
    std::string m_name {};

    // Linear sRGB color
    vec3 m_color { 1.0f };

    Transform m_transform {};
};

////////////////////////////////////////////////////////////////////////////////
// Serialization

#include "asset/SerialisationHelpers.h"
#include <cereal/cereal.hpp>

template<class Archive>
void Light::serialize(Archive& archive)
{
    archive(cereal::make_nvp("type", m_type));
    archive(cereal::make_nvp("name", m_name));

    archive(cereal::make_nvp("color", m_color));
    archive(cereal::make_nvp("transform", m_transform));

    archive(cereal::make_nvp("castsShadows", m_castsShadows));
    archive(cereal::make_nvp("customConstantBias", customConstantBias));
    archive(cereal::make_nvp("customSlopeBias", customSlopeBias));
}
