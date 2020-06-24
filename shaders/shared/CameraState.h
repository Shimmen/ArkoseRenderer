#ifndef CAMERA_STATE_H
#define CAMERA_STATE_H

struct CameraState {
    mat4 projectionFromView;
    mat4 viewFromProjection;
    mat4 viewFromWorld;
    mat4 worldFromView;
};

#endif // CAMERA_STATE_H
