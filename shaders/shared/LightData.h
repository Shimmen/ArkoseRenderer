#ifndef LIGHT_DATA_H
#define LIGHT_DATA_H

struct LightMetaData {
    int numDirectionalLights;
    int numSpotLights;
};

struct ShadowMapData {
    int textureIndex;
    int _padding0;
    int _padding1;
    int _padding2;
};

struct DirectionalLightData {

    ShadowMapData shadowMap;

    vec3 color;
    float exposure;

    vec4 worldSpaceDirection;
    vec4 viewSpaceDirection;
    mat4 lightProjectionFromWorld;
    mat4 lightProjectionFromView;
};

struct SpotLightData {

    ShadowMapData shadowMap;

    vec3 color;
    float exposure;

    vec4 worldSpaceDirection;
    vec4 viewSpaceDirection;
    mat4 lightProjectionFromWorld;
    mat4 lightProjectionFromView;

    vec4 worldSpacePosition;
    vec4 viewSpacePosition;

    vec3 worldSpaceRight;
    float outerConeHalfAngle;
    vec3 worldSpaceUp;
    int iesProfileIndex;
};

#endif // LIGHT_DATA_H
