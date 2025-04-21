#ifndef CULLING_GLSL
#define CULLING_GLSL

#define CULLING_FRUSTUM_PLANES 6

bool isSphereInFrustum(vec4 sphere, vec4 frustumPlanes[CULLING_FRUSTUM_PLANES])
{
    bool fullyOutside = false;

    for (int i = 0; i < CULLING_FRUSTUM_PLANES; ++i) {
        float signedDistance = dot(frustumPlanes[i].xyz, sphere.xyz) + frustumPlanes[i].w;
        fullyOutside = fullyOutside || (signedDistance > sphere.w);
    }

    return !fullyOutside;
}

#endif // CULLING_GLSL
