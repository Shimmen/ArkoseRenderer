#pragma once

#include "core/Badge.h"
#include "utility/Profiling.h"
#include <ark/matrix.h>
#include <ark/vector.h>
#include <ark/quaternion.h>
#include <ark/transform.h>
#include <optional>

class Scene;

class Transform {
public:

    Transform() = default;

    Transform(vec3 translation, quat orientation, vec3 scale = vec3(1.0f), const Transform* parent = nullptr)
        : m_parent(parent)
        , m_translation(translation)
        , m_orientation(orientation)
        , m_scale(scale)
    {
    }

    explicit Transform(const Transform* parent)
        : m_parent(parent)
    {
    }

    vec3 translation() const { return m_translation; }
    quat orientation() const { return m_orientation; }
    vec3 scale() const { return m_scale; }

    void set(vec3 translation, quat orientation, vec3 scale = vec3(1.0f))
    {
        m_translation = translation;
        m_orientation = orientation;
        m_scale = scale;

        // Reset matrix
        m_matrix = {};
    }

    void setFromMatrix(mat4 matrix)
    {
        ark::decomposeMatrixToTranslationRotationScale(matrix, m_translation, m_orientation, m_scale);

        // Reset matrix
        m_matrix = {};
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
        SCOPED_PROFILE_ZONE();

        if (!m_parent) {
            return localMatrix();
        }

        return m_parent->worldMatrix() * localMatrix();
    }

    mat3 worldNormalMatrix() const
    {
        SCOPED_PROFILE_ZONE();

        mat3 world3x3 = mat3(worldMatrix());
        mat3 normalMatrix = transpose(inverse(world3x3));
        return normalMatrix;
    }

    void postRender(Badge<Scene>)
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
