#pragma once

#include "core/Badge.h"
#include "utility/Profiling.h"
#include <ark/matrix.h>
#include <ark/vector.h>
#include <ark/quaternion.h>
#include <ark/transform.h>
#include <optional>

class GpuScene;

class Transform {
public:

    Transform() = default;

    Transform(vec3 translation, quat orientation, vec3 scale = vec3(1.0f), const Transform* parent = nullptr)
        : m_parent(parent)
        , m_translation(translation)
        , m_orientation(normalize(orientation))
        , m_scale(scale)
    {
    }

    explicit Transform(const Transform* parent)
        : m_parent(parent)
    {
    }

    template<class Archive>
    void load(Archive&);
    template<class Archive>
    void save(Archive&) const;

    void drawGui();

    Transform const* parent() const { return m_parent; }
    void setParent(Transform const* parent);

    Transform flattened() const;

    vec3 localTranslation() const { return m_translation; }
    quat localOrientation() const { return m_orientation; }
    vec3 localScale() const { return m_scale; }

    vec3 positionInWorld() const { return m_parent ? m_parent->worldMatrix() * m_translation : m_translation; }
    quat orientationInWorld() const { return m_parent ? m_parent->orientationInWorld() * m_orientation : m_orientation; }
    vec3 scaleInWorld() const { return m_parent ? m_parent->scaleInWorld() * m_scale : m_scale; }

    void setPositionInWorld(vec3 worldPosition);
    void setOrientationInWorld(quat worldOrientation);

    vec3 right() const { return ark::rotateVector(orientationInWorld(), ark::globalRight); }
    vec3 up() const { return ark::rotateVector(orientationInWorld(), ark::globalUp); }
    vec3 forward() const { return ark::rotateVector(orientationInWorld(), ark::globalForward); }

    void set(vec3 translation, quat orientation, vec3 scale = vec3(1.0f))
    {
        setTranslation(translation);
        setOrientation(orientation);
        setScale(scale);
        m_matrix = {};
        m_normalMatrix = {};
    }

    void setTranslation(vec3 translation)
    {
        m_translation = translation;
        m_matrix = {};
    }

    void setOrientation(quat orientation)
    {
        m_orientation = normalize(orientation);
        m_matrix = {};
        m_normalMatrix = {};
    }

    void setScale(vec3 scale)
    {
        m_scale = scale;
        m_matrix = {};
        m_normalMatrix = {};
    }

    void setScale(float scale)
    {
        m_scale = vec3(scale);
        m_matrix = {};
        m_normalMatrix = {};
    }

    void setFromMatrix(mat4 matrix)
    {
        ark::decomposeMatrixToTranslationRotationScale(matrix, m_translation, m_orientation, m_scale);

        // Reset matrix
        m_matrix = {};
        m_normalMatrix = {};
    }

    mat4 localMatrix() const
    {
        if (m_matrix.has_value() == false) {
            m_matrix = calculateLocalMatrix();
        }

        return m_matrix.value();
    }

    mat3 localNormalMatrix() const
    {
        SCOPED_PROFILE_ZONE();

        if (m_normalMatrix.has_value() == false) {
            mat3 local3x3 = mat3(localMatrix());
            m_normalMatrix = transpose(inverse(local3x3));
        }

        return m_normalMatrix.value();
    }

    mat4 worldMatrix() const
    {
        //SCOPED_PROFILE_ZONE();

        if (!m_parent) {
            return localMatrix();
        }

        return m_parent->worldMatrix() * localMatrix();
    }

    mat3 worldNormalMatrix() const
    {
        //SCOPED_PROFILE_ZONE();

        mat3 world3x3 = mat3(worldMatrix());
        mat3 normalMatrix = transpose(inverse(world3x3));
        return normalMatrix;
    }

    void postRender(Badge<GpuScene>)
    {
        m_previousFrameWorldMatrix = worldMatrix();
    }

    mat4 previousFrameWorldMatrix() const
    {
        return m_previousFrameWorldMatrix.value_or(worldMatrix());
    }

private:

    mat4 calculateLocalMatrix() const
    {
        mat4 translation = ark::translate(m_translation);
        mat4 orientation = ark::rotate(m_orientation);
        mat4 scale = ark::scale(m_scale);
        return translation * orientation * scale;
    }

    const Transform* m_parent {};

    vec3 m_translation { 0.0f };
    quat m_orientation {};
    vec3 m_scale { 1.0f };

    // Cached matrices
    mutable std::optional<mat4> m_matrix {};
    mutable std::optional<mat3> m_normalMatrix {};

    std::optional<mat4> m_previousFrameWorldMatrix{ std::nullopt };
};

class ITransformable {
public:
    virtual Transform& transform() = 0;
    //virtual Transform const& transform() const = 0;
};

////////////////////////////////////////////////////////////////////////////////
// Serialization

#include "asset/SerialisationHelpers.h"
#include <cereal/cereal.hpp>

template<class Archive>
void Transform::load(Archive& archive)
{
    archive(cereal::make_nvp("translation", m_translation));
    archive(cereal::make_nvp("orientation", m_orientation));
    archive(cereal::make_nvp("scale", m_scale));

    m_orientation = normalize(m_orientation);
}

template<class Archive>
void Transform::save(Archive& archive) const
{
    // NOTE: Parent transform in the hierarchy is never serialized, will have to be reconstructed as load-time
    archive(cereal::make_nvp("translation", m_translation));
    archive(cereal::make_nvp("orientation", normalize(m_orientation)));
    archive(cereal::make_nvp("scale", m_scale));
}
