#pragma once

#include "rendering/scene/Model.h"
#include <memory>
#include <string>
#include <tiny_gltf.h>

class GltfModel;

class GltfMesh : public Mesh {
public:
    explicit GltfMesh(std::string name, const GltfModel* parent, const tinygltf::Model&, const tinygltf::Primitive&, mat4 matrix);
    ~GltfMesh() override = default;

    const std::vector<vec3>& positionData() const override;
    const std::vector<vec2>& texcoordData() const override;
    const std::vector<vec3>& normalData() const override;
    const std::vector<vec4>& tangentData() const override;

    const std::vector<uint32_t>& indexData() const override;
    IndexType indexType() const override;
    size_t indexCount() const override;
    bool isIndexed() const override;

    moos::aabb3 boundingBox() const override { return m_aabb; }
    geometry::Sphere boundingSphere() const override { return m_boundingSphere; }

protected:
    std::unique_ptr<Material> createMaterial() override;

private:
    const tinygltf::Accessor* getAccessor(const char* name) const;

private:
    std::string m_name;
    moos::aabb3 m_aabb;
    geometry::Sphere m_boundingSphere;
    const GltfModel* m_parentModel;
    const tinygltf::Model* m_model;
    const tinygltf::Primitive* m_primitive;
};

class GltfModel : public Model {
public:
    explicit GltfModel(std::string path, const tinygltf::Model&);
    GltfModel() = default;
    ~GltfModel() = default;

    [[nodiscard]] static std::unique_ptr<Model> load(const std::string& path);

    size_t meshCount() const override;
    void forEachMesh(std::function<void(Mesh&)>) override;
    void forEachMesh(std::function<void(const Mesh&)>) const override;

    [[nodiscard]] std::string directory() const;

private:
    std::string m_path {};
    const tinygltf::Model* m_model {};
    std::vector<std::unique_ptr<GltfMesh>> m_meshes {};
};
