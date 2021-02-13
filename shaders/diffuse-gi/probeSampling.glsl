#ifndef PROBE_SAMPLING_GLSL
#define PROBE_SAMPLING_GLSL

//
// NOTE:
//
//  This is a modified version of the supplemental code from the paper titled
//     "Real-Time Global Illumination using Precomputed Light Field Probe":
//  This modified file contains only the diffuse irradiance probe parts of the
//  whole implementation. The supplemental code and the paper can be found at:
//  http://research.nvidia.com/publication/real-time-global-illumination-using-precomputed-light-field-probes
//

#include <common.glsl>
#include <common/octahedral.glsl>
#include <common/sh.glsl>
#include <common/spherical.glsl>
#include <shared/ProbeGridData.h>

ivec3 baseGridCoord(in ProbeGridData probeGrid, vec3 position)
{
    return clamp(ivec3((position - probeGrid.offsetToFirst.xyz) / probeGrid.probeSpacing.xyz),
                 ivec3(0, 0, 0),
                 ivec3(probeGrid.gridDimensions) - ivec3(1, 1, 1));
}

vec3 gridCoordToPosition(in ProbeGridData probeGrid, ivec3 gridCoord) {
    return probeGrid.offsetToFirst.xyz + vec3(gridCoord) * probeGrid.probeSpacing.xyz;
}

int gridCoordToProbeIndex(in ProbeGridData probeGrid, vec3 probeCoords) {
    return int(probeCoords.x + probeCoords.y * probeGrid.gridDimensions.x + probeCoords.z * probeGrid.gridDimensions.x * probeGrid.gridDimensions.y);
}

vec3 computePrefilteredIrradiance(vec3 wsPosition, vec3 wsNormal, in ProbeGridData gridData,
                                  sampler2DArray irradianceProbes, sampler2DArray distanceProbes)
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
        int p = gridCoordToProbeIndex(gridData, probeGridCoord);

        // Compute the trilinear weights based on the grid cell vertex to smoothly
        // transition between probes. Avoid ever going entirely to zero because that
        // will cause problems at the border probes.
        vec3 trilinear = mix(1.0 - alpha, alpha, offset);
        float weight = trilinear.x * trilinear.y * trilinear.z;

        // Make cosine falloff in tangent plane with respect to the angle from the surface to the probe so that we never
        // test a probe that is *behind* the surface.
        // It doesn't have to be cosine, but that is efficient to compute and we must clip to the tangent plane.
        vec3 probePos = gridCoordToPosition(gridData, probeGridCoord);
        vec3 pointToProbe = probePos - wsPosition;
        vec3 dir = normalize(pointToProbe);

        // Smooth back-face test
        const float smoothFloor = 0.02;
        const float additionalSmoothening = 0.25;
        weight *= smoothFloor + (1.0 - smoothFloor) * pow(saturate(dot(dir, wsNormal)), additionalSmoothening);

        vec2 distanceProbeUV = sphericalUvFromDirection(-dir);
        vec2 temp = texture(distanceProbes, vec3(distanceProbeUV, p)).rg;
        float mean = temp.x;
        float variance = abs(temp.y - square(mean));

        float distToProbe = length(pointToProbe);
        // http://www.punkuser.net/vsm/vsm_paper.pdf; equation 5
        float t_sub_mean = distToProbe - mean;
        float chebychev = variance / (variance + square(t_sub_mean));

        weight *= ((distToProbe <= mean) ? 1.0 : max(chebychev, 0.0));

        // Avoid zero weight
        weight = max(0.0002, weight);

        sumWeight += weight;

        SHVectorRGB probeSh = loadSphericalHarmonicsRGB(irradianceProbes, p);
        vec3 probeIrradiance = sampleIrradianceFromSphericalHarmonics(probeSh, normalize(wsNormal));

        sumIrradiance += weight * probeIrradiance;
    }

    return TWO_PI * sumIrradiance / sumWeight;
}

#endif // PROBE_SAMPLING_GLSL
