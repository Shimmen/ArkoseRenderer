#ifndef CAMERA_STATE_H
#define CAMERA_STATE_H

struct CameraState {
    mat4 projectionFromView;
    mat4 viewFromProjection;
    mat4 viewFromWorld;
    mat4 worldFromView;

    mat4 previousFrameProjectionFromView;
    mat4 previousFrameViewFromWorld;

    mat4 pixelFromView;
    mat4 viewFromPixel;

    vec4 frustumPlanes[6];

    vec4 renderResolution;
    vec4 outputResolution;

    float zNear;
    float zFar;
    float focalLength;
    float _pad2;

    float iso;
    float aperture; // i.e. f-number
    float shutterSpeed;
    float exposureCompensation; // for automatic exposure only
};

#endif // CAMERA_STATE_H
