#ifndef CAMERA_GLSL
#define CAMERA_GLSL

#include <common.glsl>
#include <common/culling.glsl>
#include <shared/CameraState.h>

//
// The following three functions (including the comments) are from the 'Moving Frostbite to PBR' course notes:
// https://seblagarde.files.wordpress.com/2015/07/course_notes_moving_frostbite_to_pbr_v32.pdf
//

float computeEV100(float aperture, float shutterTime, float ISO)
{
    // EV number is defined as:
    //   2^EV_s = N^2 / t and EV_s = EV_100 + log2(S/100)
    // This gives
    //   EV_s = log2(N^2 / t)
    //   EV_100 + log2(S/100) = log2 (N^2 / t)
    //   EV_100 = log2(N^2 / t) - log2(S/100)
    //   EV_100 = log2(N^2 / t . 100 / S)
    return log2(square(aperture) / shutterTime * 100.0 / ISO);
}

float computeEV100FromAvgLuminance(float avgLuminance)
{
    // We later use the middle gray at 12.7% in order to have
    // a middle gray at 18% with a sqrt (2) room for specular highlights
    // But here we deal with the spot meter measuring the middle gray
    // which is fixed at 12.5 for matching standard camera
    // constructor settings (i.e. calibration constant K = 12.5)
    // Reference : http://en.wikipedia.org/wiki/Film_speed
    return log2(avgLuminance * 100.0 / 12.5);
}

float convertEV100ToExposure(float EV100)
{
    // Compute the maximum luminance possible with H_sbs sensitivity
    // maxLum = 78 / (  S * q   ) * N^2 / t
    //        = 78 / (  S * q   ) * 2^EV_100
    //        = 78 / (100 * 0.65) * 2^EV_100
    //        = 1.2 * 2^EV
    // Reference : http://en.wikipedia.org/wiki/Film_speed
    float maxLuminance = 1.2 * pow(2.0, EV100);
    return 1.0 / maxLuminance;
}

float naturalVignetting(float falloff, float aspectRatio, vec2 uv)
{
    // From: https://github.com/keijiro/KinoVignette

    vec2 coord = (uv - vec2(0.5)) * vec2(aspectRatio, 1.0) * 2.0;
    float rf = sqrt(dot(coord, coord)) * falloff;
    float rf2_1 = rf * rf + 1.0;
    float e = 1.0 / (rf2_1 * rf2_1);

    return e;
}

struct AutoExposureResult {
    float exposure;
    float nextLuminanceHistory;
};

AutoExposureResult valueForAutomaticExposure(float avgLuminance, float luminanceHistory, float exposureCompensation, float adaptionRate, float deltaTime)
{
    // For caller: make sure to use the linear luminance value!

    // Compute the actual luminance to use from history & current
    float realLuminance = luminanceHistory + (avgLuminance - luminanceHistory) * (1.0 - exp(-adaptionRate * deltaTime));

    float ev100 = computeEV100FromAvgLuminance(realLuminance);
    ev100 -= exposureCompensation;
    float exposure = convertEV100ToExposure(ev100);

    AutoExposureResult result;
    result.exposure = exposure;
    result.nextLuminanceHistory = realLuminance;
    return result;
}

float valueForManualExposure(float aperture, float shutterSpeed, float ISO)
{
    float ev100 = computeEV100(aperture, shutterSpeed, ISO);
    float exposure = convertEV100ToExposure(ev100);
    return exposure;
}

vec3 camera_getPosition(CameraState camera)
{
    return camera.worldFromView[3].xyz;
}

ivec2 projectViewSpaceToPixel(vec3 viewSpacePos, ivec2 pixelDimensions, CameraState camera)
{
    vec4 projectedPixel = camera.pixelFromView * vec4(viewSpacePos, 1.0);
    projectedPixel.xy /= projectedPixel.w;
    return ivec2(projectedPixel.xy);
}

vec3 unprojectPixelCoordAndDepthToViewSpace(ivec2 pixelCoord, float depth, CameraState camera)
{
    vec4 viewSpacePos = camera.viewFromPixel * vec4(pixelCoord + vec2(0.5), depth, 1.0);
    viewSpacePos.xyz /= viewSpacePos.w;
    return viewSpacePos.xyz;
}

vec3 unprojectUvCoordAndDepthToViewSpace(vec2 uvCoord, float depth, CameraState camera)
{
    vec4 viewSpacePos = camera.viewFromProjection * vec4(uvCoord * 2.0 - 1.0, depth, 1.0);
    viewSpacePos.xyz /= viewSpacePos.w;
    return viewSpacePos.xyz;
}

float calculateLinearDepth(float nonlinearDepth, CameraState camera)
{
    return camera.zNear * camera.zFar / ((nonlinearDepth * (camera.zFar - camera.zNear)) - camera.zFar);
}

bool isSphereInCameraFrustum(vec4 sphere, CameraState camera)
{
    return isSphereInFrustum(sphere, camera.frustumPlanes);
}

#endif // CAMERA_GLSL
