#ifndef SHADOW_GLSL
#define SHADOW_GLSL

float evaluateDirectionalShadow(sampler2D shadowMap, mat4 lightProjectionFromView, vec3 viewSpacePos)
{
    vec4 posInShadowMap = lightProjectionFromView * vec4(viewSpacePos, 1.0);
    posInShadowMap.xyz /= posInShadowMap.w;
    vec2 shadowMapUv = posInShadowMap.xy * 0.5 + 0.5;

    // TODO: Fix this! I think the reason we have to do this here is because we have texture repeat and linear filtering!
    const float eps = 0.01;
    if (shadowMapUv.x < eps || shadowMapUv.y < eps || shadowMapUv.x > 1.0-eps || shadowMapUv.y > 1.0-eps) {
        return 1.0;
    }

    float mapDepth = texture(shadowMap, shadowMapUv).x;

    // This isn't optimal but it works for now
    vec2 pixelSize = 1.0 / textureSize(shadowMap, 0);
    float bias = max(pixelSize.x, pixelSize.y) + 0.006;

    // (remember: 1 is furthest away, 0 is closest!)
    return (mapDepth < posInShadowMap.z - bias) ? 0.0 : 1.0;
}

float evaluateShadow(sampler2D shadowMap, mat4 lightProjectionFromView, vec3 viewSpacePos)
{
    vec4 posInShadowMap = lightProjectionFromView * vec4(viewSpacePos, 1.0);
    posInShadowMap.xyz /= posInShadowMap.w;

    vec2 shadowMapUv = posInShadowMap.xy * 0.5 + 0.5;
    float mapDepth = texture(shadowMap, shadowMapUv).x;

    // TODO: Use smarter bias!
    const float bias = 1e-4;
    return (mapDepth < posInShadowMap.z - bias) ? 0.0 : 1.0;
}

#endif // SHADOW_GLSL
