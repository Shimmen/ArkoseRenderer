#include "Model.h"

VertexLayout Mesh::canonoicalVertexLayout()
{
    return VertexLayout {
        sizeof(CanonoicalVertex),
        { { 0, VertexAttributeType::Float3, offsetof(CanonoicalVertex, position) },
          { 1, VertexAttributeType::Float2, offsetof(CanonoicalVertex, texCoord) },
          { 2, VertexAttributeType ::Float3, offsetof(CanonoicalVertex, normal) },
          { 3, VertexAttributeType ::Float4, offsetof(CanonoicalVertex, tangent) } }
    };
}

bool Model::hasProxy() const
{
    return m_proxy != nullptr;
}

const Model& Model::proxy() const
{
    if (hasProxy()) {
        return *m_proxy;
    }

    // If no proxy is set, use self as proxy
    return *this;
}

void Model::setProxy(std::unique_ptr<Model> proxy)
{
    m_proxy = std::move(proxy);
}
