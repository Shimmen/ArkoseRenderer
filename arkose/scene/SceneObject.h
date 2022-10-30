#pragma once

#include "asset/StaticMeshAsset.h"
#include "scene/Transform.h"
#include <memory>
#include <string>
#include <variant>

class SceneObject {
public:
    SceneObject() = default;
    ~SceneObject() = default;

    template<class Archive>
    void serialize(Archive&);

    bool hasPathToMesh() const { return std::holds_alternative<std::string>(mesh); }
    std::string_view pathToMesh() const
    {
        ARKOSE_ASSERT(hasPathToMesh());
        return std::get<std::string>(mesh);
    }

    std::string name {};
    Transform transform {};

    // Path to a mesh or an mesh asset directly
    // TODO: Convert static mesh asset!
    //std::variant<std::string, std::weak_ptr<StaticMeshAsset>> mesh;
    std::variant<std::string, int> mesh;
};

////////////////////////////////////////////////////////////////////////////////
// Serialization

#include <cereal/cereal.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/variant.hpp>

template<class Archive>
void SceneObject::serialize(Archive& archive)
{
    archive(cereal::make_nvp("name", name));
    archive(cereal::make_nvp("transform", transform));
    archive(cereal::make_nvp("mesh", mesh));
}
