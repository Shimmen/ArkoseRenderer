#pragma once

#include "backend/Resources.h"
#include "rendering/scene/Material.h"
#include "rendering/scene/Transform.h"
#include "rendering/scene/Vertex.h"
#include <mooslib/vector.h>
#include <unordered_map>

class Model;

class Mesh {
public:
    explicit Mesh(Transform transform)
        : m_transform(transform)
    {
    }
    virtual ~Mesh() = default;

    virtual void setModel(Model* model) { m_owner = model; }
    virtual Model* model() { return m_owner; }
    virtual const Model* model() const { return m_owner; }

    virtual const Transform& transform() const { return m_transform; }

    Material& material();

    void ensureVertexBuffer(const SemanticVertexLayout&);
    const Buffer& vertexBuffer(const SemanticVertexLayout&);

    void ensureIndexBuffer();
    const Buffer& indexBuffer();

    // TODO: Remove this vvvvv
    struct CanonoicalVertex {
        vec3 position;
        vec2 texCoord;
        vec3 normal;
        vec4 tangent;
    };

    static VertexLayout canonoicalVertexLayout();
    virtual std::vector<CanonoicalVertex> canonoicalVertexData() const = 0;
    // TODO: Remove this ^^^^^

    virtual const std::vector<vec3>& positionData() const = 0;
    virtual const std::vector<vec2>& texcoordData() const = 0;
    virtual const std::vector<vec3>& normalData() const = 0;
    virtual const std::vector<vec4>& tangentData() const = 0;

    virtual const std::vector<uint32_t>& indexData() const = 0;
    virtual IndexType indexType() const = 0;
    virtual size_t indexCount() const = 0;
    virtual bool isIndexed() const = 0;

protected:
    // CPU data cache
    // TODO: Remove this vvvvv
    mutable std::optional<std::vector<CanonoicalVertex>> m_canonoicalVertexData;
    // TODO: Remove this ^^^^^
    mutable std::optional<std::vector<vec3>> m_positionData;
    mutable std::optional<std::vector<vec2>> m_texcoordData;
    mutable std::optional<std::vector<vec3>> m_normalData;
    mutable std::optional<std::vector<vec4>> m_tangentData;
    mutable std::optional<std::vector<uint32_t>> m_indexData;

    // GPU Buffer cache
    mutable const Buffer* m_indexBuffer { nullptr };
    mutable std::unordered_map<SemanticVertexLayout, const Buffer*> m_vertexBuffers;

    virtual std::unique_ptr<Material> createMaterial() = 0;
    std::unique_ptr<Material> m_material {};

private:
    Transform m_transform {};
    Model* m_owner { nullptr };
};
