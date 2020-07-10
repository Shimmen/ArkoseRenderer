#version 460

#include <shared/CameraState.h>
#include <shared/Picking.h>

layout(location = 0) in vec3 aPosition;

layout(set = 0, binding = 0) uniform CameraBlock {
    CameraState camera;
};

layout(set = 0, binding = 1) uniform TransformDataBlock{
    mat4 transforms[PICKING_MAX_DRAWABLES];
};

layout(location = 0) flat out uint vIndex;

void main()
{
    uint objectIndex = gl_InstanceIndex;
    vIndex = objectIndex;

    mat4 worldFromLocal = transforms[objectIndex];
    gl_Position = camera.projectionFromView * camera.viewFromWorld * worldFromLocal * vec4(aPosition, 1.0);
}
