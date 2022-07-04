#ifndef SHADOW_GLSL
#define SHADOW_GLSL

float evaluateShadow(sampler2D shadowMap, mat4 lightProjectionFromView, vec3 viewSpacePos)
{
    vec4 posInShadowMap = lightProjectionFromView * vec4(viewSpacePos, 1.0);
    posInShadowMap.xyz /= posInShadowMap.w;

    vec2 shadowMapUv = posInShadowMap.xy * 0.5 + 0.5;
    float mapDepth = texture(shadowMap, shadowMapUv).x;

    return (mapDepth < posInShadowMap.z) ? 0.0 : 1.0;
}

#endif // SHADOW_GLSL
