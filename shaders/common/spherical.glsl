#ifndef SPHERICAL_GLSL
#define SPHERICAL_GLSL

#include <common.glsl>

vec2 sphericalUvFromDirection(vec3 direction)
{
    float phi = atan(direction.z, direction.x);
    float theta = acos(clamp(direction.y, -1.0, +1.0));

    if (phi < 0.0) phi += TWO_PI;
	return vec2(phi / TWO_PI, theta / PI);
}

vec3 directionFromSphericalUv(vec2 uv)
{
    float phi = uv.x * TWO_PI;
    float theta = uv.y * PI;

    float sinTheta = sin(theta);

    return vec3(
        cos(phi) * sinTheta,
        cos(theta),
        sin(phi) * sinTheta
    );
}

#endif // SPHERICAL_GLSL
