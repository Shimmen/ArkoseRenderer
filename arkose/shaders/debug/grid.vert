#version 460

#include <common/camera.glsl>

layout(set = 0, binding = 0) uniform CameraStateBlock { CameraState camera; };

layout(location = 0) out vec3 vPosition;
layout(location = 1) out float vRadius;

const vec2 positions[6] = vec2[6](vec2(-1.0, -1.0),
                                  vec2(+1.0, -1.0),
                                  vec2(+1.0, +1.0),
                                  vec2(-1.0, -1.0),
                                  vec2(+1.0, +1.0),
                                  vec2(-1.0, +1.0));

const float gridSize = 50.0;

void main()
{

    vec2 position2d = positions[gl_VertexIndex] * gridSize + camera_getPosition(camera).xz;

    vPosition = vec3(position2d.x, 0.0, position2d.y);
    vRadius = gridSize;

    gl_Position = camera.unjitteredProjectionFromView * camera.viewFromWorld * vec4(vPosition, 1.0);
}
