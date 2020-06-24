#version 450
#extension GL_KHR_vulkan_glsl : enable

#include <shared/CameraState.h>
#include <shared/ForwardData.h>

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec3 aNormal;
layout(location = 3) in vec4 aTangent;

layout(binding = 0) uniform CameraStateBlock
{
    CameraState camera;
};

layout(binding = 1) uniform PerObjectBlock
{
    PerForwardObject perObject[FORWARD_MAX_DRAWABLES];
};

layout(location = 0) out vec2 vTexCoord;
layout(location = 1) out vec3 vPosition;
layout(location = 2) out vec3 vNormal;
layout(location = 3) out mat3 vTbnMatrix;
layout(location = 10) flat out int vMaterialIndex;

void main()
{
    int objectIndex = gl_InstanceIndex;
    PerForwardObject object = perObject[objectIndex];
    vMaterialIndex = object.materialIndex;

    vec4 viewSpacePos = camera.viewFromWorld * object.worldFromLocal * vec4(aPosition, 1.0);
    vPosition = viewSpacePos.xyz;

    mat3 viewFromTangent = mat3(camera.viewFromWorld) * mat3(object.worldFromTangent);
    vec3 viewSpaceNormal = normalize(viewFromTangent * aNormal);
    vec3 viewSpaceTangent = normalize(viewFromTangent * aTangent.xyz);
    vec3 viewSpaceBitangent = cross(viewSpaceNormal, viewSpaceTangent) * aTangent.w;
    vTbnMatrix = mat3(viewSpaceTangent, viewSpaceBitangent, viewSpaceNormal);
    vNormal = viewSpaceNormal;

    vTexCoord = aTexCoord;

    gl_Position = camera.projectionFromView * viewSpacePos;
}
