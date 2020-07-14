#pragma once

#include "rendering/scene/Material.h"
#include "rendering/scene/Mesh.h"
#include "rendering/scene/Transform.h"
#include <functional>
#include <mooslib/vector.h>

class Scene;

class Model {
public:
    Model() = default;
    virtual ~Model() = default;

    const std::string& name() const { return m_name; }
    void setName(std::string name) { m_name = std::move(name); }

    Transform& transform() { return m_transform; }
    const Transform& transform() const { return m_transform; }

    virtual size_t meshCount() const = 0;
    virtual void forEachMesh(std::function<void(Mesh&)>) = 0;
    virtual void forEachMesh(std::function<void(const Mesh&)>) const = 0;

    bool hasProxy() const;
    const Model& proxy() const;
    void setProxy(std::unique_ptr<Model>);

    virtual void setScene(Badge<Scene>, Scene* scene) { m_scene = scene; }
    virtual const Scene* scene() const { return m_scene; }
    virtual Scene* scene() { return m_scene; }

private:
    Scene* m_scene { nullptr };

    std::string m_name;
    Transform m_transform {};
    std::unique_ptr<Model> m_proxy {};
};
