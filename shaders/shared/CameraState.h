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

    float iso;
    float aperture;
    float shutterSpeed;
    float exposureCompensation; // for automatic exposure only
};

#endif // CAMERA_STATE_H
