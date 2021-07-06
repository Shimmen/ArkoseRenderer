#version 460
#extension GL_NV_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_scalar_block_layout : require

#include <shared/RTData.h>
#include <shared/SceneData.h>

layout(location = 0) rayPayloadInNV vec3 hitValue;
hitAttributeNV vec3 attribs;

struct Vertex {
	vec3 normal;
	vec2 texCoord;
};

layout(set = 1, binding = 0, scalar) buffer readonly TriangleMeshes { RTTriangleMesh meshes[]; };
layout(set = 1, binding = 1, scalar) buffer readonly Indices        { uint indices[]; };
layout(set = 1, binding = 2, scalar) buffer readonly Vertices       { Vertex vertices[]; };

layout(set = 2, binding = 0) buffer readonly MaterialBlock { ShaderMaterial materials[]; };
layout(set = 2, binding = 1) uniform sampler2D textures[SCENE_MAX_TEXTURES];

void main()
{
	RTTriangleMesh mesh = meshes[gl_InstanceCustomIndexNV];
	ShaderMaterial material = materials[mesh.materialIndex];

	ivec3 idx = ivec3(indices[mesh.firstIndex + 3 * gl_PrimitiveID + 0],
					  indices[mesh.firstIndex + 3 * gl_PrimitiveID + 1],
					  indices[mesh.firstIndex + 3 * gl_PrimitiveID + 2]);

	Vertex v0 = vertices[mesh.firstVertex + idx.x];
	Vertex v1 = vertices[mesh.firstVertex + idx.y];
	Vertex v2 = vertices[mesh.firstVertex + idx.z];

	const vec3 b = vec3(1.0 - attribs.x - attribs.y, attribs.x, attribs.y);

	vec3 N = normalize(v0.normal.xyz * b.x + v1.normal.xyz * b.y + v2.normal.xyz * b.z);
	mat3 normalMatrix = transpose(mat3(gl_WorldToObjectNV));
	N = normalize(normalMatrix * N);

	vec2 uv = v0.texCoord.xy * b.x + v1.texCoord.xy * b.y + v2.texCoord.xy * b.z;
	vec3 baseColor = texture(textures[material.baseColor], uv).rgb;

	//hitValue = N * 0.5 + 0.5;
	//hitValue = vec3(uv, 0.0);
	hitValue = baseColor;
}
