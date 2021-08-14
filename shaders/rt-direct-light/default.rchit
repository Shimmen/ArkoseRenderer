#version 460
#extension GL_NV_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_scalar_block_layout : require

#include <rt-direct-light/common.glsl>
#include <shared/SceneData.h>

layout(location = 0) rayPayloadInNV RayPayload payload;
hitAttributeNV vec3 attribs;

layout(location = 1) rayPayloadNV bool inShadow;

layout(set = 0, binding = 0) uniform accelerationStructureNV topLevelAS;

layout(set = 1, binding = 0, scalar) buffer readonly TriangleMeshes { RTTriangleMesh meshes[]; };
layout(set = 1, binding = 1, scalar) buffer readonly Indices        { uint indices[]; };
layout(set = 1, binding = 2, scalar) buffer readonly Vertices       { Vertex vertices[]; };

layout(set = 2, binding = 0) buffer readonly MaterialBlock { ShaderMaterial materials[]; };
layout(set = 2, binding = 1) uniform sampler2D textures[SCENE_MAX_TEXTURES];

// TODO: It would be nice to actually support the names uniforms API for ray tracing..
layout(push_constant) uniform PushConstantBlock
{
	PushConstants pushConstants;
};

bool hitPointInShadow(vec3 L, float lightDistance)
{
	vec3 hitPoint = gl_WorldRayOriginNV + gl_HitTNV * gl_WorldRayDirectionNV;
	uint flags = gl_RayFlagsTerminateOnFirstHitNV | gl_RayFlagsSkipClosestHitShaderNV | gl_RayFlagsOpaqueNV;
	uint cullMask = 0xff;

	const int shadowPayloadIdx = 1;

	// Assume we are in shadow, and if the shadow miss shader activates we are *not* in shadow
	inShadow = true;

	traceNV(topLevelAS, flags, cullMask,
			0, // sbtRecordOffset
			0, // sbtRecordStride
			1, // missIndex
			hitPoint, 0.001, L, lightDistance,
			shadowPayloadIdx);

	return inShadow;
}

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

	vec3 color = vec3(0.0);
	color += pushConstants.ambientAmount * baseColor;

	//payload.color = N * 0.5 + 0.5;
	//payload.color = vec3(uv, 0.0);
	payload.color = color;
	payload.hitT = gl_HitTNV;
}
