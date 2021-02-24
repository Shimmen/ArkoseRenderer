#ifndef LIGHT_DATA_H
#define LIGHT_DATA_H

struct DirectionalLightData {
    vec4 colorAndIntensity;
    vec4 worldSpaceDirection;
    vec4 viewSpaceDirection;
    mat4 lightProjectionFromWorld;
    mat4 lightProjectionFromView;
};

#endif // LIGHT_DATA_H
