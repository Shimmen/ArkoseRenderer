#ifndef LIGHT_DATA_H
#define LIGHT_DATA_H

struct LightMetaData {
    int numDirectionalLights;
    int numSpotLights;
};

struct DirectionalLightData {

    vec3 color;
    float exposure;

    vec4 worldSpaceDirection;
    vec4 viewSpaceDirection;
    mat4 lightProjectionFromWorld;
    mat4 lightProjectionFromView;
};

struct SpotLightData {

    vec3 color;
    float exposure;

    vec4 worldSpaceDirection;
    vec4 viewSpaceDirection;
    mat4 lightProjectionFromWorld;
    mat4 lightProjectionFromView;

    vec4 worldSpacePosition;
    vec4 viewSpacePosition;

    float outerConeHalfAngle;
    int iesProfileIndex;
    vec2 _pad0;
};

#endif // LIGHT_DATA_H
