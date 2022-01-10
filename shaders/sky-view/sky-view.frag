#version 460

#include <shared/CameraState.h>
#include <common/namedUniforms.glsl>
#include <common/spherical.glsl>

layout(location = 0) in vec4 vPosition;
layout(location = 1) in vec3 vViewRay;

layout(set = 0, binding = 0) uniform CameraStateBlock { CameraState camera; };
layout(set = 0, binding = 1) uniform sampler2D environmentTex;

NAMED_UNIFORMS(pushConstants,
    float environmentMultiplier;
)

layout(location = 0) out vec4 oColor;
layout(location = 1) out vec4 oVelocity;

void main()
{
    // Draw environment map color
    {
        vec3 viewRay = normalize(vViewRay);
        vec2 sampleUv = sphericalUvFromDirection(viewRay);
        vec3 environment = texture(environmentTex, sampleUv).rgb;

        // Account for the smaller solid angle of pixels near the poles
        // Note that we don't actually get a solid angle out, since we plug 1.0 in as reference.
        float sampleWeight = sphericalMappingPixelSolidAngle(viewRay, 1.0);
        environment *= sampleWeight;

        environment *= pushConstants.environmentMultiplier;

        oColor = vec4(environment, 1.0);
    }

    // Draw velocity for environment / distant sky
    {
        vec4 viewSpacePos = camera.viewFromProjection * vPosition;
        viewSpacePos.xyz /= viewSpacePos.w;
        vec3 worldSpacePos = mat3(camera.worldFromView) * viewSpacePos.xyz;

        vec3 previousViewSpacePos = mat3(camera.previousFrameViewFromWorld) * worldSpacePos;
        vec4 previousFrameProjectedPos = camera.previousFrameProjectionFromView * vec4(previousViewSpacePos, 1.0);
        previousFrameProjectedPos /= previousFrameProjectedPos.w;

        vec2 velocity = (vPosition.xy - previousFrameProjectedPos.xy) * vec2(0.5, 0.5); // in uv-space

        //velocity = abs(velocity) * 100.0; // debug code
        oVelocity = vec4(velocity, 0.0, 0.0);
    }
}
