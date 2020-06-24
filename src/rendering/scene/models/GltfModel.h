#pragma once

#include "rendering/scene/Model.h"
#include <memory>
#include <string>
#include <tiny_gltf.h>

class GltfModel;

class GltfMesh : public Mesh {
public:
    explicit GltfMesh(std::string name, const GltfModel* parent, const tinygltf::Model&, const tinygltf::Primitive&, mat4 matrix);
    ~GltfMesh() = default;

    Material material() const override;

    std::vector<CanonoicalVertex> canonoicalVertexData() const override;

    const std::vector<vec3>& positionData() const override;
    const std::vector<vec2>& texcoordData() const override;
    const std::vector<vec3>& normalData() const override;
    const std::vector<vec4>& tangentData() const override;

    const std::vector<uint32_t>& indexData() const override;
    size_t indexCount() const override;
    bool isIndexed() const override;

    VertexFormat vertexFormat() const override;
    IndexType indexType() const override;

private:
    const tinygltf::Accessor* getAccessor(const char* name) const;

private:
    std::string m_name;
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

    void forEachMesh(std::function<void(const Mesh&)>) const override;

    [[nodiscard]] std::string directory() const;

private:
    std::string m_path {};
    const tinygltf::Model* m_model {};
    std::vector<GltfMesh> m_meshes {};
};
