#version 460

#include <common.glsl>
#include <common/namedUniforms.glsl>
#include <hair/hairCommon.glsl>

layout(set = 1, binding = 0) uniform sampler2D hairShadowMapTex;

NAMED_UNIFORMS(constants,
    mat4 projectionFromWorld;
    float shadowMapDepthRange;
)

layout(location = 0) out vec4 oColor;

void main()
{
    vec2 invShadowMapSize = 1.0 / vec2(textureSize(hairShadowMapTex, 0));
    vec2 shadowMapUv = (gl_FragCoord.xy - vec2(0.5)) * invShadowMapSize;

    float frontDepth = texture(hairShadowMapTex, shadowMapUv).r;
    float strandDepth = gl_FragCoord.z;

    float depthDelta = strandDepth - frontDepth;
    if (depthDelta < 0.0) {
        discard;
    }

    float worldDepthOffset = depthDelta * constants.shadowMapDepthRange;
    int layer = int(floor(worldDepthOffset / HAIR_DOM_LAYER_THICKNESS));

    // Clamp the layer to the valid range. This is necessary for hair-like objects that have
    // hair in the front & back (but a head in the middle), to ensure the back parts are not
    // discarded. However, for a dense volume of hair, this will cause some artifacts as all
    // depth beyond HAIR_DOM_LAYER_COUNT * HAIR_DOM_LAYER_THICKNESS are discarded. The real
    // fix for that would be to support more layers though, which is far out of scope.
    layer = clamp(layer, 0, HAIR_DOM_LAYER_COUNT - 1);

    // TODO: Use real hair opacity values!
    float strandOpacity = 0.028;

    vec4 layerOpacity = vec4(0.0);
    layerOpacity[layer] = strandOpacity;

    oColor = layerOpacity;
}
