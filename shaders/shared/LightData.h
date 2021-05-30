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

struct PerLightShadowData {

    // TODO: It would be nice if we could combine this into the LightData stuff so we don't have to have so many buffers around!

    mat4 lightViewFromWorld;
    mat4 lightProjectionFromWorld;

    float constantBias;
    float slopeBias;
    float _pad0;
    float _pad1;
    
};

#endif // LIGHT_DATA_H
