#ifndef COMMON_GLSL
#define COMMON_GLSL

#define PI     (3.14159265358979323846)
#define TWO_PI (2.0 * PI)

#define GOLDEN_RATIO (1.618034) // ((1.0 + sqrt(5.0)) / 2.0)
#define GOLDEN_ANGLE (2.399963)

#include <shared/SphericalHarmonics.h>

void swap(inout float a, inout float b)
{
    float tmp = a;
    a = b;
    b = tmp;
}

float luminance(vec3 color)
{
    return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

float lengthSquared(vec2 v)
{
    return dot(v, v);
}

float lengthSquared(vec3 v)
{
    return dot(v, v);
}

float lengthSquared(vec4 v)
{
    return dot(v, v);
}

float distanceSquared(vec2 a, vec2 b)
{
    return lengthSquared(a - b);
}

float distanceSquared(vec3 a, vec3 b)
{
    return lengthSquared(a - b);
}

float square(float x)
{
    return x * x;
}

vec3 square(vec3 x)
{
    return x * x;
}

float saturate(float x)
{
    return clamp(x, 0.0, 1.0);
}

vec2 saturate(vec2 x)
{
    return clamp(x, vec2(0.0), vec2(1.0));
}

vec3 saturate(vec3 x)
{
    return clamp(x, vec3(0.0), vec3(1.0));
}

float maxComponent(vec2 v)
{
    return max(v.x, v.y);
}

float maxComponent(vec3 v)
{
    return max(max(v.x, v.y), v.z);
}

void reortogonalize(in vec3 v0, inout vec3 v1)
{
    // Perform Gram-Schmidt's re-ortogonalization process to make v1 orthagonal to v0
    v1 = normalize(v1 - dot(v1, v0) * v0);
}

// Tom Duff et al. 2017, "Building an Orthonormal Basis, Revisited" https://jcgt.org/published/0006/01/01/
void createOrthonormalBasis(in vec3 n, out vec3 b1, out vec3 b2)
{
    float zSign = sign(n.z);
    const float a = -1.0 / (zSign + n.z);
    const float b = n.x * n.y * a;
    b1 = vec3(1.0 + zSign * n.x * n.x * a, zSign * b, -zSign * n.x);
    b2 = vec3(b, zSign + n.y * n.y * a, -n.y);
}

vec2 hammersley(uint i, uint n)
{
    uint bits = i;
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    float xi1 = float(bits) * 2.3283064365386963e-10;// / 0x100000000

    float xi0 = float(i) / float(n);
    return vec2(xi0, xi1);
}

vec3 sphericalFibonacciSample(uint i, uint n)
{
	// Refer to sphericalFibonacci(..) in c++ source for more info
	
	float theta = TWO_PI * float(i) / GOLDEN_RATIO;
	float phi = acos(2.0 * (float(i) / float(n)) - 1.0);

	float sinPhi = sin(phi);
	return vec3(cos(theta) * sinPhi, sin(theta) * sinPhi, cos(phi));
}

// https://en.wikipedia.org/wiki/Rodrigues%27_rotation_formula
vec3 axisAngleRotate(vec3 target, vec3 axis, float angle)
{
    const vec3 k = axis;
    const vec3 v = target;

    const float sinTheta = sin(angle);
    const float cosTheta = cos(angle);

    return v * cosTheta + cross(k, v) * sinTheta + k * dot(k, v) * (1.0 - cosTheta);
}

vec3 sampleSphericalHarmonic(SphericalHarmonics sh, vec3 dir)
{
    // See https://github.com/google/spherical-harmonics/blob/master/sh/spherical_harmonics.cc#L103
    // This function uses second order SH

    float Y00     = +0.282095;

    float Y1_1    = -0.488603 * dir.y;
    float Y10     = +0.488603 * dir.z;
    float Y11     = -0.488603 * dir.x;

    float Y2_2    = +1.092548 * dir.x * dir.y;
    float Y2_1    = -1.092548 * dir.y * dir.z;
    float Y20     = +0.315392 * (-dir.x * dir.x - dir.y * dir.y + 2.0 * dir.z * dir.z);
    float Y21     = +1.092548 * dir.x * dir.z;
    float Y22     = +0.546274 * (dir.x * dir.x - dir.y * dir.y);

    return Y00  * sh.L00.xyz
         + Y1_1 * sh.L1_1.xyz +  Y10 * sh.L10.xyz  + Y11 * sh.L11.xyz
         + Y2_2 * sh.L2_2.xyz + Y2_1 * sh.L2_1.xyz + Y20 * sh.L20.xyz + Y21 * sh.L21.xyz + Y22 * sh.L22.xyz;
}

// Source: http://lolengine.net/blog/2013/07/27/rgb-to-hsv-in-glsl
vec3 hsv2rgb(vec3 c)
{
    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

vec4 transformSphere(in vec4 sphere, mat4 M)
{
    mat4 Mt = transpose(M);
    float scaleX2 = lengthSquared(Mt[0].xyz);
    float scaleY2 = lengthSquared(Mt[1].xyz);
    float scaleZ2 = lengthSquared(Mt[2].xyz);

    float newRadius = sphere.w * sqrt(max(scaleX2, max(scaleY2, scaleZ2)));
    vec3 newCenter = vec3(M * vec4(sphere.xyz, 1.0));

    return vec4(newCenter, newRadius);
}

#endif// COMMON_GLSL
