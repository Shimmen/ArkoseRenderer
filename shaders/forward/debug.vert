#version 460

#include <shared/CameraState.h>
#include <shared/SceneData.h>

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec3 aNormal;
layout(location = 3) in vec4 aTangent;

layout(set = 0, binding = 0) uniform CameraStateBlock { CameraState camera; };
layout(set = 1, binding = 0) uniform PerObjectBlock { ShaderDrawable perObject[SCENE_MAX_DRAWABLES]; };

layout(location = 0) out vec2 vTexCoord;
layout(location = 1) out vec3 vPosition;
layout(location = 2) out vec3 vNormal;
layout(location = 3) flat out int vMaterialIndex;

void main()
{
    // TODO: Get this from a vertex buffer instead!
    int objectIndex = gl_InstanceIndex;

    ShaderDrawable object = perObject[objectIndex];
    vMaterialIndex = object.materialIndex;

    vec4 viewSpacePos = camera.viewFromWorld * object.worldFromLocal * vec4(aPosition, 1.0);
    vec3 viewSpaceNormal = normalize(mat3(camera.viewFromWorld) * mat3(object.worldFromTangent) * aNormal);

    vPosition = viewSpacePos.xyz;
    vNormal = viewSpaceNormal;
    vTexCoord = aTexCoord;

    gl_Position = camera.projectionFromView * viewSpacePos;
}
