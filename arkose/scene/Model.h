#pragma once

#include "rendering/Material.h"
#include "scene/Mesh.h"
#include "scene/Transform.h"
#include <functional>
#include <ark/vector.h>

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

private:
    std::string m_name;
    Transform m_transform {};
    std::unique_ptr<Model> m_proxy {};
};
