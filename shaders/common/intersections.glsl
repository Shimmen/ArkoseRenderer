#ifndef INTERSECTIONS_GLSL
#define INTERSECTIONS_GLSL

#include <common.glsl>

bool raySphereIntersection(vec3 center, float radius, vec3 origin, vec3 direction, out float t)
{
	vec3 relOrigin = origin - center;

	// (quadratic formula)
	float a = dot(direction, direction);
	float b = 2.0 * dot(direction, relOrigin);
	float c = dot(relOrigin, relOrigin) - square(radius);

	float descriminant = b * b - 4.0 * a * c;
	if (descriminant < 0.0) {
		return false;
	}

	t = (-b - sqrt(descriminant)) / (2.0 * a);
	if (t >= gl_RayTminNV && t <= gl_RayTmaxNV) {
		return true;
	}

	t = (-b + sqrt(descriminant)) / (2.0 * a);
	if (t >= gl_RayTminNV && t <= gl_RayTmaxNV) {
		return true;
	}

	return false;
}

#define PLANE_DOUBLE_SIDED 1
bool rayPlaneIntersection(vec3 N, float d, vec3 origin, vec3 direction, out float t)
{
	float denom = dot(N, direction);

	#if PLANE_DOUBLE_SIDED
	if (abs(denom) < 1e-6) {
		return false;
	}
	#else
	if (denom > 1e-6) {
		return false;
	}
	#endif

	t = -(d + dot(N, origin)) / denom;
	return t >= 0.0;
}

bool rayAabbIntersection(vec3 aabbMin, vec3 aabbMax, vec3 rayOrigin, vec3 rayDirection, out float tMin, out float tMax)
{
	float tmin = (aabbMin.x - rayOrigin.x) / rayDirection.x;
	float tmax = (aabbMax.x - rayOrigin.x) / rayDirection.x;

	if (tmin > tmax)
		swap(tmin, tmax);

	float tymin = (aabbMin.y - rayOrigin.y) / rayDirection.y;
	float tymax = (aabbMax.y - rayOrigin.y) / rayDirection.y;

	if (tymin > tymax)
		swap(tymin, tymax);

	if ((tmin > tymax) || (tymin > tmax))
		return false;

	if (tymin > tmin)
		tmin = tymin;

	if (tymax < tmax)
		tmax = tymax;

	float tzmin = (aabbMin.z - rayOrigin.z) / rayDirection.z;
	float tzmax = (aabbMax.z - rayOrigin.z) / rayDirection.z;

	if (tzmin > tzmax)
		swap(tzmin, tzmax);

	if ((tmin > tzmax) || (tzmin > tmax))
		return false;

	if (tzmin > tmin)
		tmin = tzmin;

	if (tzmax < tmax)
		tmax = tzmax;

	tMin = tmin;
	tMax = tmax;
	return true;
}

#endif // INTERSECTIONS_GLSL
