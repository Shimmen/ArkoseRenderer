#version 460

#include <shared/CameraState.h>
#include <shared/SceneData.h>

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec3 aNormal;

layout(set = 0, binding = 0) uniform CameraBlock { CameraMatrices cameras[6]; };
layout(set = 3, binding = 0) buffer readonly ObjectBlock { ShaderDrawable perObject[]; };

layout(location = 0) out vec3 vPosition;
layout(location = 1) out vec2 vTexCoord;
layout(location = 2) out vec3 vNormal;
layout(location = 3) flat out int vMaterialIndex;

layout(push_constant) uniform PushConstants {
    uint sideIndex;
    float ambientLx;
};

void main()
{
    // TODO: Get this from a vertex buffer instead!
    int objectIndex = gl_InstanceIndex;

    CameraMatrices camera = cameras[sideIndex];

    ShaderDrawable object = perObject[objectIndex];
    vMaterialIndex = object.materialIndex;

    vec4 viewSpacePos = camera.viewFromWorld * object.worldFromLocal * vec4(aPosition, 1.0);
    vec3 viewSpaceNormal = normalize(mat3(camera.viewFromWorld) * mat3(object.worldFromTangent) * aNormal);

    vPosition = viewSpacePos.xyz;
    vNormal = viewSpaceNormal;
    vTexCoord = aTexCoord;

    gl_Position = camera.projectionFromView * viewSpacePos;
}
