#version 460
#extension GL_NV_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_scalar_block_layout : require

#include <shared/RTData.h>

layout(location = 0) rayPayloadInNV vec3 hitValue;
hitAttributeNV vec3 attribs;

layout(binding = 0, set = 1, scalar) buffer readonly Meshes   { RTMesh meshes[]; };
layout(binding = 1, set = 1, scalar) buffer readonly Vertices { RTVertex x[]; } vertices[];
layout(binding = 2, set = 1)         buffer readonly Indices  { uint idx[]; }  indices[];
layout(binding = 3, set = 1) uniform sampler2D baseColorSamplers[RT_MAX_TEXTURES];

void unpack(out RTMesh mesh, out RTVertex v0, out RTVertex v1, out RTVertex v2)
{
	mesh = meshes[gl_InstanceCustomIndexNV];
	uint objId = mesh.objectId;

	ivec3 idx = ivec3(indices[objId].idx[3 * gl_PrimitiveID + 0],
					  indices[objId].idx[3 * gl_PrimitiveID + 1],
					  indices[objId].idx[3 * gl_PrimitiveID + 2]);

	v0 = vertices[objId].x[idx.x];
	v1 = vertices[objId].x[idx.y];
	v2 = vertices[objId].x[idx.z];
}

void main()
{
	RTMesh mesh;
	RTVertex v0, v1, v2;
	unpack(mesh, v0, v1, v2);

	const vec3 b = vec3(1.0 - attribs.x - attribs.y, attribs.x, attribs.y);

	vec3 N = normalize(v0.normal.xyz * b.x + v1.normal.xyz * b.y + v2.normal.xyz * b.z);
	mat3 normalMatrix = transpose(mat3(gl_WorldToObjectNV));
	N = normalize(normalMatrix * N);

	vec2 uv = v0.texCoord.xy * b.x + v1.texCoord.xy * b.y + v2.texCoord.xy * b.z;
	vec3 baseColor = texture(baseColorSamplers[mesh.baseColor], uv).rgb;

	//hitValue = N * 0.5 + 0.5;
	//hitValue = vec3(uv, 0.0);
	hitValue = baseColor;
}
