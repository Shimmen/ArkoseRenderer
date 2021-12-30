#ifndef DDGI_COMMON_GLSL
#define DDGI_COMMON_GLSL

#include <common.glsl>
#include <common/random.glsl>
#include <shared/RTData.h>
#include <shared/DDGIData.h>

#define HIT_T_MISS (-1.0)

#define DDGI_IRRADIANCE_GAMMA     (2.2)
#define DDGI_IRRADIANCE_INV_GAMMA (1.0 / DDGI_IRRADIANCE_GAMMA)

struct Vertex {
	vec3 normal;
	vec2 texCoord;
};

struct RayPayload {
	vec3 color;
	float hitT;
};

struct RayTracingPushConstants {
	float ambientAmount;
	float environmentMultiplier;
	uint frameIdx;
};

vec3 calculateRotatedSphericalFibonacciSample(uint probeIdx, uint sampleIdx, uint sampleCount, uint frameIdx)
{
    vec3 sampleDir = sphericalFibonacciSample(sampleIdx, sampleCount);

    // Seed the rng uniquely for this combination of probe & frame
    const uint paramSpacing = 64;
    seedRandom(paramSpacing * probeIdx + frameIdx % paramSpacing);

    // Grab random values enough to define a rotation
    vec3 axis = randomPointOnSphere();
    float angle = TWO_PI * randomFloat();

    return axisAngleRotate(sampleDir, axis, angle);
}

struct AtlasCoords
{
    // The index of the sheet that this probe will be found in
    uint sheetIdx;

    // The coordinates of the probe tile (not pixels) within its sheet
    ivec2 sheetCoord;
};

AtlasCoords calculateAtlasCoords(uint probeIdx, ivec3 gridDimensions)
{
    // A single sheet is a single xz-plane of probes (x=width, y=height, z=depth)
    const uint tilesPerSheet = gridDimensions.x * gridDimensions.z;

    // The index of the probe within its sheet
    uint sheetProbeIdx = probeIdx % tilesPerSheet;

    AtlasCoords atlasCoords;

    atlasCoords.sheetIdx = probeIdx / tilesPerSheet;
    atlasCoords.sheetCoord = ivec2(sheetProbeIdx % gridDimensions.x,
                                  sheetProbeIdx / gridDimensions.x);

    return atlasCoords;
}

ivec2 calculateAtlasTexelCoord(uint probeIdx, ivec3 gridDimensions, ivec2 tileTexelCoord, int tileResolution, int tilePadding)
{
    AtlasCoords atlasCoords = calculateAtlasCoords(probeIdx, gridDimensions);

    // The coordinates of the probe tile (not pixels) globally
    ivec2 probeAtlasTileCoord = atlasCoords.sheetCoord + ivec2(atlasCoords.sheetIdx * gridDimensions.x, 0);

    // The coordinate of the first texel in this tile (initial padding + tile + padding after tile + padding before next tile)
    ivec2 probeTileFirstTexel = ivec2(tilePadding) + probeAtlasTileCoord * ivec2(tileResolution + 2 * tilePadding);

    // The final texel coordinate for this texel (tileTexelCoord = texel coord within tile)
    ivec2 atlasTexelCoord = probeTileFirstTexel + tileTexelCoord;

    return atlasTexelCoord;
}

vec3 calculateProbePosition(in DDGIProbeGridData probeData, uint probeIdx)
{
    AtlasCoords atlasCoords = calculateAtlasCoords(probeIdx, probeData.gridDimensions.xyz);

    // The global coordinates of the probe
    ivec3 probeCoord = ivec3(atlasCoords.sheetCoord.x, atlasCoords.sheetIdx, atlasCoords.sheetCoord.y);

    return probeData.offsetToFirst.xyz + vec3(probeCoord * probeData.probeSpacing.xyz);
}

#endif // DDGI_COMMON_GLSL
