#ifndef DDGI_PROBE_SAMPLING_GLSL
#define DDGI_PROBE_SAMPLING_GLSL

#include <common.glsl>
#include <common/octahedral.glsl>
#include <ddgi/common.glsl>
#include <shared/DDGIData.h>

vec2 calculateAtlasSampleUV(in DDGIProbeGridData gridData, ivec3 probeGridCoord, vec3 direction, int tileResolution, int tilePadding, vec2 invAtlasTextureSize)
{
    // The coordinates of the probe tile (not pixels) globally
    ivec2 probeAtlasTileCoord = ivec2(probeGridCoord.x + probeGridCoord.y * gridData.gridDimensions.x, probeGridCoord.z);

    // The coordinate of the first texel in this tile (initial padding + tile + padding after tile + padding before next tile)
    ivec2 probeTileFirstTexel = ivec2(tilePadding) + probeAtlasTileCoord * ivec2(tileResolution + 2 * tilePadding);

    // The local texel coords within the tile, corresponding to the octahedral representation of our direction
    vec2 tileTexelCoord = (octahedralEncode(direction) * 0.5 + 0.5) * vec2(tileResolution);

    // The global texel coordinate for this texel (tileTexelCoord = texel coord within tile)
    vec2 atlasTexelCoord = vec2(probeTileFirstTexel) + tileTexelCoord;

    // The UV coordinates for sampling the atlas for this proble (probe grid coord) and direction
    vec2 atlasSampleUV = atlasTexelCoord * invAtlasTextureSize;

    return atlasSampleUV;
}

vec3 sampleIrradianceProbeRaw(in DDGIProbeGridData gridData, ivec3 probeGridCoord, vec3 direction, sampler2D irradianceAtlas)
{
    // TODO: We should just pass this in:
    vec2 invAtlasTextureSize = vec2(1.0) / textureSize(irradianceAtlas, 0);

    vec2 uv = calculateAtlasSampleUV(gridData, probeGridCoord, direction, DDGI_IRRADIANCE_RES, DDGI_ATLAS_PADDING, invAtlasTextureSize);
    return texture(irradianceAtlas, uv).rgb;
}

vec3 sampleIrradianceProbe(in DDGIProbeGridData gridData, ivec3 probeGridCoord, vec3 direction, sampler2D irradianceAtlas)
{
    return pow(sampleIrradianceProbeRaw(gridData, probeGridCoord, direction, irradianceAtlas), vec3(DDGI_IRRADIANCE_GAMMA));
}

vec2 sampleVisibilityProbe(in DDGIProbeGridData gridData, ivec3 probeGridCoord, vec3 direction, sampler2D visibilityAtlas)
{
    // TODO: We should just pass this in:
    vec2 invAtlasTextureSize = vec2(1.0) / textureSize(visibilityAtlas, 0);

    vec2 uv = calculateAtlasSampleUV(gridData, probeGridCoord, direction, DDGI_VISIBILITY_RES, DDGI_ATLAS_PADDING, invAtlasTextureSize);
    return texture(visibilityAtlas, uv).xy;
}

ivec3 baseGridCoord(in DDGIProbeGridData probeGrid, vec3 position)
{
    return clamp(ivec3((position - probeGrid.offsetToFirst.xyz) / probeGrid.probeSpacing.xyz),
                 ivec3(0, 0, 0),
                 ivec3(probeGrid.gridDimensions) - ivec3(1, 1, 1));
}

vec3 gridCoordToPosition(in DDGIProbeGridData probeGrid, ivec3 gridCoord)
{
    return probeGrid.offsetToFirst.xyz + vec3(gridCoord) * probeGrid.probeSpacing.xyz;
}

vec3 sampleDynamicDiffuseGlobalIllumination(vec3 wsPosition, vec3 wsNormal, vec3 wsView, in DDGIProbeGridData gridData,
                                            in sampler2D irradianceAtlas, in sampler2D visibilityAtlas)
{
    ivec3 baseGridCoord = baseGridCoord(gridData, wsPosition);
    vec3 baseProbePos = gridCoordToPosition(gridData, baseGridCoord);

    vec3 sumIrradiance = vec3(0.0);
    float sumWeight = 0.0;

    // Trilinear interpolation values along axes
    vec3 alpha = clamp((wsPosition - baseProbePos) / gridData.probeSpacing.xyz, vec3(0.0), vec3(1.0));

    // Iterate over the adjacent probes defining the surrounding vertex "cage"
    for (int i = 0; i < 8; ++i) {

        // Compute the offset grid coord and clamp to the probe grid boundary
        ivec3 offset = ivec3(i, i >> 1, i >> 2) & ivec3(1);
        ivec3 probeGridCoord = clamp(baseGridCoord + offset, ivec3(0), ivec3(gridData.gridDimensions - 1));

        // Compute the trilinear weights based on the grid cell vertex to smoothly
        // transition between probes. Avoid ever going entirely to zero because that
        // will cause problems at the border probes.
        vec3 trilinear = max(vec3(0.001), mix(1.0 - alpha, alpha, offset));
        float trilinearWeight = trilinear.x * trilinear.y * trilinear.z;

        float weight = 1.0;

        const float tunableShadowBias = 0.3; // TODO: make tunable!
        float minDistanceBetweenProbes = min(gridData.probeSpacing.x, min(gridData.probeSpacing.y, gridData.probeSpacing.z));
        vec3 selfShadowBias = (wsNormal * 0.2 + wsView * 0.8) * (0.75 * minDistanceBetweenProbes) * tunableShadowBias;
        vec3 biasedPosition = wsPosition + selfShadowBias;

        // Make cosine falloff in tangent plane with respect to the angle from the surface to the probe so that we never
        // test a probe that is *behind* the surface.
        // It doesn't have to be cosine, but that is efficient to compute and we must clip to the tangent plane.
        vec3 probePos = gridCoordToPosition(gridData, probeGridCoord);
        vec3 pointToProbe = probePos - biasedPosition;
        vec3 directionToProbe = normalize(pointToProbe);

#if 0
        // Smooth back-face test (from original paper)
        const float smoothFloor = 0.02;
        const float additionalSmoothening = 0.25;
        weight *= smoothFloor + (1.0 - smoothFloor) * pow(saturate(dot(directionToProbe, wsNormal)), additionalSmoothening);
#else
        // "The naive soft backface weight would ignore a probe when it is behind the surface. That's good for walls. But for small details inside of a
        //  room, the normals on the details might rule out all of the probes that have mutual visibility to the point. So, we instead use a "wrap shading"
        //  test below inspired by NPR work.".
        const vec3 sampleDirection = wsNormal;
        weight *= square((dot(directionToProbe, sampleDirection) + 1.0) * 0.5) + 0.2;
#endif

        // Chebychev test (i.e. variance shadow test)
        {
            vec2 visData = sampleVisibilityProbe(gridData, probeGridCoord, -directionToProbe, visibilityAtlas);
            float meanDistanceToOccluder = visData.x;
            float variance = abs(visData.y - square(visData.x));
            float distToProbe = length(pointToProbe);

            float chebychevWeight = 1.0;
            if (distToProbe > meanDistanceToOccluder) {
                chebychevWeight = variance / (variance + square(distToProbe - meanDistanceToOccluder));
                chebychevWeight = chebychevWeight * chebychevWeight * chebychevWeight; // increase contrast in the weight
            }

            chebychevWeight = max(0.05, chebychevWeight);
            weight *= chebychevWeight;
        }

        // Avoid zero weight
        weight = max(0.000001, weight);

        // "A tiny bit of light is really visible due to log perception, so crush tiny weights but keep the curve continuous."
        const float crushThreshold = 0.2;
        if (weight < crushThreshold) {
            weight *= square(weight) * (1.0 / square(crushThreshold)); 
        }

        weight *= trilinearWeight;

        // NOTE: we sample the raw variant here, since we do the decoding ourselves below
        vec3 probeIrradiance = sampleIrradianceProbeRaw(gridData, probeGridCoord, normalize(wsNormal), irradianceAtlas);

        // "Decode the tone curve, but leave a gamma = 2 curve (=sqrt here) to approximate sRGB blending for the trilinear"
        probeIrradiance = pow(probeIrradiance, vec3(DDGI_IRRADIANCE_GAMMA * 0.5));

        sumIrradiance += weight * probeIrradiance;
        sumWeight += weight;
    }

    vec3 irradiance = sumIrradiance / sumWeight;

    // Extract linear irradiance (see gamma decoding above)
    irradiance = square(irradiance);

    irradiance *= 0.5 * PI;

    return irradiance;
}

#endif // DDGI_PROBE_SAMPLING_GLSL
