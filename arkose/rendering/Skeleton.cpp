#include "Skeleton.h"

#include "asset/SkeletonAsset.h"

SkeletonJoint::SkeletonJoint(SkeletonJointAsset const& jointAsset)
    : m_name(jointAsset.name)
    , m_index(jointAsset.index)
    , m_transform(jointAsset.transform)
    , m_invBindMatrix(jointAsset.invBindMatrix)
{
    m_childJoints.reserve(jointAsset.children.size());
    for (SkeletonJointAsset const& childJointAsset : jointAsset.children) {
        SkeletonJoint& childJoint = m_childJoints.emplace_back(childJointAsset);
        childJoint.transform().setParent(&m_transform);
    }
}

SkeletonJoint::~SkeletonJoint() = default;

Skeleton::Skeleton(SkeletonAsset const* skeletonAsset)
    : m_rootJoint(skeletonAsset->rootJoint)
    , m_maxJointIdx(skeletonAsset->maxJointIdx)
{
    ARKOSE_ASSERT(m_rootJoint.transform().parent() == nullptr);
}

Skeleton::~Skeleton() = default;

Transform* Skeleton::findTransformForJoint(std::string_view jointName)
{
    // TODO: Optimize! This could be very slow for large skeletons

    std::vector<SkeletonJoint*> pendingJoints {};
    pendingJoints.push_back(&m_rootJoint);

    while (pendingJoints.size() > 0 && pendingJoints.back() != nullptr) {
        SkeletonJoint* joint = pendingJoints.back();
        pendingJoints.pop_back();

        if (joint->name() == jointName) {
            return &joint->transform();
        }

        for (SkeletonJoint& childJoint : joint->childJoints()) {
            pendingJoints.push_back(&childJoint);
        }
    }

    return nullptr;
}

void Skeleton::applyJointTransformations()
{
    SCOPED_PROFILE_ZONE();

    const size_t jointMatrixCount = m_maxJointIdx + 1;
    m_appliedJointMatrices.resize(jointMatrixCount);
    m_appliedJointTangentMatrices.resize(jointMatrixCount);

    // TODO: Make non-recursive implemementation
    std::function<void(SkeletonJoint const&)> applyJointTransformationsRecursively = [&](SkeletonJoint const& joint) {

        mat4 animatedPoseMatrix = joint.transform().worldMatrix();

        mat4 jointMatrix = animatedPoseMatrix * joint.invBindMatrix();
        mat3 jointTangentMatrix = transpose(inverse(mat3(jointMatrix)));

        ARKOSE_ASSERT(joint.index() < jointMatrixCount);
        m_appliedJointMatrices[joint.index()] = jointMatrix;
        m_appliedJointTangentMatrices[joint.index()] = jointTangentMatrix;

        for (SkeletonJoint const& childJoint : joint.childJoints()) {
            applyJointTransformationsRecursively(childJoint);
        }
    };

    applyJointTransformationsRecursively(m_rootJoint);
}

std::vector<mat4> const& Skeleton::appliedJointMatrices() const
{
    ARKOSE_ASSERT(m_appliedJointMatrices.size() == m_maxJointIdx + 1);
    return m_appliedJointMatrices;
}

std::vector<mat3> const& Skeleton::appliedJointTangentMatrices() const
{
    ARKOSE_ASSERT(m_appliedJointTangentMatrices.size() == m_maxJointIdx + 1);
    return m_appliedJointTangentMatrices;
}

SkeletonJoint const& Skeleton::rootJoint() const
{
    return m_rootJoint;
}

void Skeleton::debugPrintState() const
{
    fmt::print("Skeleton:\n");

    std::function<void(SkeletonJoint const&, std::string)> recursivelyPrintJoints = [&](SkeletonJoint const& joint, std::string indent) {
        vec3 t = joint.transform().localTranslation();
        quat r = joint.transform().localOrientation();
        fmt::print("{}{} => translation=({:.4f},{:.4f},{:.4f}), rotation=({:.4f},{:.4f},{:.4f},{:.4f})\n",
                   indent, joint.name(),
                   t.x, t.y, t.z,
                   r.vec.x, r.vec.y, r.vec.z, r.w);

        for (SkeletonJoint const& childJoint : joint.childJoints()) {
            recursivelyPrintJoints(childJoint, indent + " ");
        }
    };

    recursivelyPrintJoints(m_rootJoint, " ");
}
