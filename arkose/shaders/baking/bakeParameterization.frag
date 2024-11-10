#version 460

#extension GL_EXT_fragment_shader_barycentric : require

layout(location = 0) out uint outTriangleIdx;
layout(location = 1) out vec4 outBarycentricCoords;

void main()
{
    outTriangleIdx = gl_PrimitiveID + 1; // 0 => no triangle
    outBarycentricCoords = vec4(gl_BaryCoordEXT, 1.0);
}
