#pragma once

#include "rendering/backend/Resources.h"
#include "scene/editor/EditorObject.h"
#include <ark/color.h>
#include <ark/matrix.h>

class LightAsset;

enum class ShadowMode {
    None,
    ShadowMapped,
    RayTraced,
};

class Light : public IEditorObject {
public:

    enum class Type {
        DirectionalLight,
        SphereLight,
        SpotLight,
    };

    Light() = default;
    Light(Type type, Color color);
    Light(Type type, LightAsset const&);

    virtual ~Light() { }

    Color color() const { return m_color; }
    void setColor(Color color) { m_color = color; }

    Type type() const { return m_type; }

    // ITransformable interface
    Transform& transform() override { return m_transform; }
    Transform const& transform() const { return m_transform; }

    // IEditorObject interface
    virtual bool shouldDrawGui() const override;
    virtual void drawGui() override;

    virtual mat4 lightViewMatrix() const;

    virtual float intensityValue() const = 0;
    virtual mat4 projectionMatrix() const = 0;

    mat4 viewProjection() const { return projectionMatrix() * lightViewMatrix(); };

    float customConstantBias = 0.0f;
    float customSlopeBias = 0.0f;

    virtual float constantBias() const = 0;
    virtual float slopeBias() const = 0;

    virtual bool supportsShadowMode(ShadowMode) const = 0;

    ShadowMode shadowMode() const { return m_shadowMode; }
    bool castsShadows() const { return shadowMode() != ShadowMode::None; }

    void setName(std::string name) { m_name = std::move(name); }
    const std::string& name() const { return m_name; }

private:
    Type m_type { Type::DirectionalLight };
    ShadowMode m_shadowMode { ShadowMode::ShadowMapped };
    std::string m_name {};

    Color m_color { Colors::white };

    Transform m_transform {};
};
