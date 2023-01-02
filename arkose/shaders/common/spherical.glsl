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

float sphericalMappingPixelSolidAngle(vec3 dir, float uniformPixelSolidAngleRef)
{
    // TODO: This is measured by eye and is completely arbitrary really. I know for sure
    //  that the peak at the poles should be much more intense (i.e., lower solid angle)
    //  but this simple formula seems sufficient for now.. If I ever get a response to
    //  https://twitter.com/SimonMoos/status/1310589288883552258 that would help :)
    return mix(uniformPixelSolidAngleRef, 0.6 * uniformPixelSolidAngleRef, pow(abs(dir.y), 4.0));
}

#endif // SPHERICAL_GLSL
