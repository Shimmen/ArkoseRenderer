#include "Mesh.h"

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
