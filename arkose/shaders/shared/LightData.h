#ifndef LIGHT_DATA_H
#define LIGHT_DATA_H

struct LightMetaData {
    bool hasDirectionalLight;
    uint numSphereLights;
    uint numSpotLights;
};

struct DirectionalLightData {

    vec3 color;
    float exposure;

    vec4 worldSpaceDirection;
    vec4 viewSpaceDirection;
    mat4 lightProjectionFromWorld;
    mat4 lightProjectionFromView;
};

struct SphereLightData {

    vec3 color;
    float exposure;

    vec4 worldSpacePosition;
    vec4 viewSpacePosition;

    vec2 _pad0;
    float lightRadius;
    float lightSourceRadius;
};

struct SpotLightData {

    vec3 color;
    float exposure;

    vec4 worldSpaceDirection;
    vec4 viewSpaceDirection;
    mat4 lightProjectionFromWorld;
    mat4 lightProjectionFromView;

    vec4 worldSpaceRight;
    vec4 worldSpaceUp;
    vec4 viewSpaceRight;
    vec4 viewSpaceUp;

    vec4 worldSpacePosition;
    vec4 viewSpacePosition;

    float outerConeHalfAngle;
    int iesProfileIndex;
    vec2 _pad0;
};

#endif // LIGHT_DATA_H
