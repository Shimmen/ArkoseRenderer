#ifndef SH_GLSL
#define SH_GLSL

#include <common.glsl>

// NOTE: Everything here assumes L2 (two band) SH

struct SHVector
{
    float e00;
    float e11;
    float e10;
    float e1_1;
    float e21;
    float e2_1;
    float e2_2;
    float e20;
    float e22;
};

struct SHVectorRGB
{
    vec3 e00;
    vec3 e11;
    vec3 e10;
    vec3 e1_1;
    vec3 e21;
    vec3 e2_1;
    vec3 e2_2;
    vec3 e20;
    vec3 e22;
};

SHVector createSphericalHarmonicsBasis(vec3 N)
{
    N = normalize(N);

    SHVector basis;
    basis.e00  = 0.282095;
    basis.e11  = 0.488603 * N.x;
    basis.e10  = 0.488603 * N.z;
    basis.e1_1 = 0.488603 * N.y;
    basis.e21  = 1.092548 * N.x * N.z;
    basis.e2_1 = 1.092548 * N.y * N.z;
    basis.e2_2 = 1.092548 * N.y * N.x;
    basis.e20  = 0.946176 * N.z * N.z - 0.315392;
    basis.e22  = 0.546274 * (N.x * N.x - N.y * N.y);

    return basis;
}

SHVector createSphericalHarmonicsZero()
{
    SHVector sh;
    sh.e00  = 0.0;
    sh.e11  = 0.0;
    sh.e10  = 0.0;
    sh.e1_1 = 0.0;
    sh.e21  = 0.0;
    sh.e2_1 = 0.0;
    sh.e2_2 = 0.0;
    sh.e20  = 0.0;
    sh.e22  = 0.0;
    return sh;
}

SHVectorRGB createSphericalHarmonicsZeroRGB()
{
    SHVectorRGB sh;
    sh.e00  = vec3(0, 0, 0);
    sh.e11  = vec3(0, 0, 0);
    sh.e10  = vec3(0, 0, 0);
    sh.e1_1 = vec3(0, 0, 0);
    sh.e21  = vec3(0, 0, 0);
    sh.e2_1 = vec3(0, 0, 0);
    sh.e2_2 = vec3(0, 0, 0);
    sh.e20  = vec3(0, 0, 0);
    sh.e22  = vec3(0, 0, 0);
    return sh;
}

vec3 sampleIrradianceFromSphericalHarmonics(SHVectorRGB sh, vec3 N)
{
    SHVector Y = createSphericalHarmonicsBasis(N);

    // Used for extracting irradiance from the SH, see paper:
    // https://graphics.stanford.edu/papers/envmap/envmap.pdf
    float A0 = PI;
    float A1 = 2.0 / 3.0 * PI;
    float A2 = 1.0 / 4.0 * PI;

    return A0*Y.e00*sh.e00
        + A1*Y.e1_1*sh.e1_1 + A1*Y.e10*sh.e10 + A1*Y.e11*sh.e11
        + A2*Y.e2_2*sh.e2_2 + A2*Y.e2_1*sh.e2_1 + A2*Y.e20*sh.e20 + A2*Y.e21*sh.e21 + A2*Y.e22*sh.e22;
}

SHVectorRGB loadSphericalHarmonicsRGB(sampler2DArray map, int layer)
{
    SHVectorRGB sh;

    sh.e00  = texelFetch(map, ivec3(0,0,layer), 0).rgb;
    sh.e11  = texelFetch(map, ivec3(1,0,layer), 0).rgb;
    sh.e10  = texelFetch(map, ivec3(2,0,layer), 0).rgb;
    sh.e1_1 = texelFetch(map, ivec3(0,1,layer), 0).rgb;
    sh.e21  = texelFetch(map, ivec3(1,1,layer), 0).rgb;
    sh.e2_1 = texelFetch(map, ivec3(2,1,layer), 0).rgb;
    sh.e2_2 = texelFetch(map, ivec3(0,2,layer), 0).rgb;
    sh.e20  = texelFetch(map, ivec3(1,2,layer), 0).rgb;
    sh.e22  = texelFetch(map, ivec3(2,2,layer), 0).rgb;

    return sh;
}

void storeSphericalHarmonicsRGB(SHVectorRGB sh, image2D map)
{
    imageStore(map, ivec2(0,0), vec4(sh.e00,  0.0));
    imageStore(map, ivec2(1,0), vec4(sh.e11,  0.0));
    imageStore(map, ivec2(2,0), vec4(sh.e10,  0.0));
    imageStore(map, ivec2(0,1), vec4(sh.e1_1, 0.0));
    imageStore(map, ivec2(1,1), vec4(sh.e21,  0.0));
    imageStore(map, ivec2(2,1), vec4(sh.e2_1, 0.0));
    imageStore(map, ivec2(0,2), vec4(sh.e2_2, 0.0));
    imageStore(map, ivec2(1,2), vec4(sh.e20,  0.0));
    imageStore(map, ivec2(2,2), vec4(sh.e22,  0.0));
}

#endif // SH_GLSL
