#pragma once

#include "rendering/backend/util/DrawCall.h"
#include "rendering/backend/Resources.h"
#include "core/math/Sphere.h"
#include "rendering/Material.h"
#include "scene/Transform.h"
#include "scene/Vertex.h"
#include <ark/aabb.h>
#include <ark/vector.h>
#include <unordered_map>

class Model;
class GpuScene;

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

    Material& material();
    virtual Transform& transform() { return m_transform; }
    virtual const Transform& transform() const { return m_transform; }

    std::optional<int> materialIndex() const { return m_materialIndex; }
    void setMaterialIndex(Badge<GpuScene>, int index) { m_materialIndex = index; }

    virtual ark::aabb3 boundingBox() const = 0;
    virtual geometry::Sphere boundingSphere() const = 0;

    void ensureDrawCallIsAvailable(const VertexLayout&, GpuScene&);
    const DrawCallDescription& drawCallDescription(const VertexLayout&, GpuScene&);

    std::vector<uint8_t> vertexData(const VertexLayout&) const;
    size_t vertexCountForLayout(const VertexLayout&) const;

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
    mutable std::optional<std::vector<vec3>> m_positionData;
    mutable std::optional<std::vector<vec2>> m_texcoordData;
    mutable std::optional<std::vector<vec3>> m_normalData;
    mutable std::optional<std::vector<vec4>> m_tangentData;
    mutable std::optional<std::vector<uint32_t>> m_indexData;

    mutable std::unordered_map<VertexLayout, DrawCallDescription> m_drawCallDescriptions;

    virtual std::unique_ptr<Material> createMaterial() = 0;
    std::unique_ptr<Material> m_material {};

private:
    Transform m_transform {};
    Model* m_owner { nullptr };

    std::optional<int> m_materialIndex {};
};