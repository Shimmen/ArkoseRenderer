#ifndef LIGHT_DATA_H
#define LIGHT_DATA_H

struct DirectionalLight {
    vec4 colorAndIntensity;
    vec4 worldSpaceDirection;
    vec4 viewSpaceDirection;
    mat4 lightProjectionFromWorld;
};

#endif // LIGHT_DATA_H
