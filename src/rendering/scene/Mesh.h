#pragma once

#include "backend/Resources.h"
#include "rendering/scene/Material.h"
#include "rendering/scene/Transform.h"
#include <mooslib/vector.h>

class Model;

class Mesh {
public:
    Mesh(Transform transform)
        : m_transform(transform)
    {
    }
    virtual ~Mesh() = default;

    virtual void setModel(Model* model) { m_owner = model; }
    virtual Model* model() { return m_owner; }
    virtual const Model* model() const { return m_owner; }

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

    virtual const std::vector<uint32_t>& indexData() const = 0;
    virtual IndexType indexType() const = 0;
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
    Model* m_owner { nullptr };
};
