#version 460

#include <shared/CameraState.h>
#include <shared/SceneData.h>

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec3 aNormal;
layout(location = 3) in vec4 aTangent;

layout(set = 0, binding = 0) uniform CameraStateBlock { CameraState camera; };
layout(set = 3, binding = 0) buffer readonly PerObjectBlock { ShaderDrawable perObject[]; };

layout(location = 0) flat out int vMaterialIndex;
layout(location = 1) out vec2 vTexCoord;
layout(location = 2) out vec3 vPosition;
layout(location = 3) out vec4 vCurrFrameProjectedPos;
layout(location = 4) out vec4 vPrevFrameProjectedPos;
layout(location = 5 /*, 6, 7*/) out mat3 vTbnMatrix;

void main()
{
    ShaderDrawable object = perObject[gl_DrawID];
    vMaterialIndex = object.materialIndex;

    vTexCoord = aTexCoord;

    vec4 viewSpacePos = camera.viewFromWorld * object.worldFromLocal * vec4(aPosition, 1.0);
    vPosition = viewSpacePos.xyz;

    vCurrFrameProjectedPos = camera.projectionFromView * viewSpacePos;
    vPrevFrameProjectedPos = camera.previousFrameProjectionFromView * camera.previousFrameViewFromWorld * object.previousFrameWorldFromLocal * vec4(aPosition, 1.0);

    mat3 viewFromTangent = mat3(camera.viewFromWorld) * mat3(object.worldFromTangent);
    vec3 viewSpaceNormal = normalize(viewFromTangent * aNormal);
    vec3 viewSpaceTangent = normalize(viewFromTangent * aTangent.xyz);
    vec3 viewSpaceBitangent = cross(viewSpaceNormal, viewSpaceTangent) * aTangent.w;
    vTbnMatrix = mat3(viewSpaceTangent, viewSpaceBitangent, viewSpaceNormal);

    gl_Position = vCurrFrameProjectedPos;
}
