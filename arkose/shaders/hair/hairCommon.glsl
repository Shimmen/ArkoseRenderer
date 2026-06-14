#ifndef HAIR_COMMON_GLSL
#define HAIR_COMMON_GLSL

struct HairInterpolants {
    uint strandIndex;
};

struct HairConstants {
    bool withMaterialColor;
    uint drawableIdx;
    uint firstStrand;
    uint strandCount;
};

// DOM = Deep Opacity Map
#define HAIR_DOM_LAYER_COUNT 4
#define HAIR_DOM_LAYER_THICKNESS 0.02

float hairEvaluateDeepOpacityTransmittance(
    sampler2D hairShadowMapTex,
    sampler2D hairDeepOpacityMapTex,
    mat4 lightProjectionFromView,
    vec3 viewSpacePos,
    float shadowMapDepthRange)
{
    vec4 posInLightClip = lightProjectionFromView * vec4(viewSpacePos, 1.0);
    posInLightClip.xyz /= posInLightClip.w;

    vec2 domUv = posInLightClip.xy * 0.5 + 0.5;
    if (any(lessThan(domUv, vec2(0.0))) || any(greaterThan(domUv, vec2(1.0)))) {
        return 1.0;
    }

    float frontDepth = texture(hairShadowMapTex, domUv).r;
    float depthDelta = posInLightClip.z - frontDepth;
    if (depthDelta < 0.0) {
        return 1.0;
    }

    float worldDepthOffset = depthDelta * shadowMapDepthRange;
    float normalizedDepth = worldDepthOffset / HAIR_DOM_LAYER_THICKNESS;
    int layer = clamp(int(floor(normalizedDepth)), 0, HAIR_DOM_LAYER_COUNT - 1);
 
    float fractionInLayer = fract(normalizedDepth);
    if (normalizedDepth >= float(HAIR_DOM_LAYER_COUNT - 1)) {
        fractionInLayer = 1.0;
    }

    vec4 dom = texture(hairDeepOpacityMapTex, domUv);

    float transmittance = 1.0;
    for (int i = 0; i < layer; ++i) {
        transmittance *= (1.0 - saturate(dom[i]));
    }
    transmittance *= (1.0 - saturate(dom[layer]) * fractionInLayer);

    return transmittance;
}

#endif // HAIR_COMMON_GLSL
