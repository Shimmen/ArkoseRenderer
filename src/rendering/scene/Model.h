#pragma once

#include "backend/Resources.h"
#include "rendering/Registry.h"
#include "rendering/camera/FpsCamera.h"
#include "rendering/scene/Material.h"
#include "rendering/scene/Transform.h"
#include <functional>
#include <mooslib/vector.h>

class Mesh {
public:
    Mesh(Transform transform)
        : m_transform(transform)
    {
    }
    virtual ~Mesh() = default;

    virtual const Transform& transform() const { return m_transform; }
    // TODO: Don't recreate new material on each request
    virtual Material material() const = 0;

    struct CanonoicalVertex {
        vec3 position;
        vec2 texCoord;
        vec3 normal;
        vec4 tangent;
    };

    static VertexLayout canonoicalVertexLayout();
    virtual std::vector<CanonoicalVertex> canonoicalVertexData() const = 0;

    virtual const std::vector<vec3>& positionData() const = 0;
    virtual const std::vector<vec2>& texcoordData() const = 0;
    virtual const std::vector<vec3>& normalData() const = 0;
    virtual const std::vector<vec4>& tangentData() const = 0;

    virtual VertexFormat vertexFormat() const = 0;
    virtual IndexType indexType() const = 0;

    virtual const std::vector<uint32_t>& indexData() const = 0;
    virtual size_t indexCount() const = 0;
    virtual bool isIndexed() const = 0;

protected:
    // Data cache
    mutable std::optional<std::vector<CanonoicalVertex>> m_canonoicalVertexData;
    mutable std::optional<std::vector<vec3>> m_positionData;
    mutable std::optional<std::vector<vec2>> m_texcoordData;
    mutable std::optional<std::vector<vec3>> m_normalData;
    mutable std::optional<std::vector<vec4>> m_tangentData;
    mutable std::optional<std::vector<uint32_t>> m_indexData;

private:
    Transform m_transform {};
};

class Model {
public:
    Model() = default;
    virtual ~Model() = default;

    const std::string& name() const { return m_name; }
    void setName(std::string name) { m_name = std::move(name); }

    Transform& transform() { return m_transform; }
    const Transform& transform() const { return m_transform; }

    virtual size_t meshCount() const = 0;
    virtual void forEachMesh(std::function<void(const Mesh&)>) const = 0;

    bool hasProxy() const;
    const Model& proxy() const;
    void setProxy(std::unique_ptr<Model>);

private:
    std::string m_name;
    Transform m_transform {};
    std::unique_ptr<Model> m_proxy {};
};
