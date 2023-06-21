#pragma once

#include "core/Types.h"
#include "scene/Transform.h"
#include <ark/matrix.h>
#include <string>
#include <string_view>
#include <vector>

class SkeletonAsset;
class SkeletonJointAsset;

class SkeletonJoint {
public:
    SkeletonJoint(SkeletonJointAsset const&);
    ~SkeletonJoint();

    std::string_view name() const { return m_name; }
    u32 index() const { return m_index; }

    Transform const& transform() const { return m_transform; }
    Transform& transform() { return m_transform; }

    mat4 const& invBindMatrix() const { return m_invBindMatrix; }

    std::vector<SkeletonJoint> const& childJoints() const { return m_childJoints; }
    std::vector<SkeletonJoint>& childJoints() { return m_childJoints; }

private:
    std::string m_name {}; // for referencing by name
    u32 m_index {}; // for referencing from vertex by index

    Transform m_transform {};
    mat4 m_invBindMatrix {};

    std::vector<SkeletonJoint> m_childJoints {};
};

class Skeleton {
public:
    Skeleton(SkeletonAsset const*);
    ~Skeleton();

    Transform* findTransformForJoint(std::string_view jointName);
    void applyJointTransformations();

    std::vector<mat4> const& appliedJointMatrices() const;
    std::vector<mat3> const& appliedJointTangentMatrices() const;

    void debugPrintState() const;

private:
    SkeletonJoint m_rootJoint;

    size_t m_maxJointIdx { 0 };

    std::vector<mat4> m_appliedJointMatrices {}; // for position transformation
    std::vector<mat3> m_appliedJointTangentMatrices {}; // for tangent-space direction transformations (e.g. normals)
};
