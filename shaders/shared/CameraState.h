#ifndef CAMERA_STATE_H
#define CAMERA_STATE_H

struct CameraMatrices {
    mat4 projectionFromView;
    mat4 viewFromProjection;
    mat4 viewFromWorld;
    mat4 worldFromView;
};

struct CameraState {
    mat4 projectionFromView;
    mat4 viewFromProjection;
    mat4 viewFromWorld;
    mat4 worldFromView;

    mat4 previousFrameProjectionFromView;
    mat4 previousFrameViewFromWorld;

    mat4 pixelFromView;
    mat4 viewFromPixel;

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
