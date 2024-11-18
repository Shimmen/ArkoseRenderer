#ifndef G_ENCODING_GLSL
#define G_ENCODING_GLSL

#include <common/octahedral.glsl>

vec2 encodeNormal(vec3 N)
{
    return octahedralEncode(N);
}

vec2 encodeNullNormal()
{
    // TODO: Is this the ideal value?
    return vec2(0.0, 0.0);
}

vec3 decodeNormal(vec2 encodedNormal)
{
    return octahedralDecode(encodedNormal);
}

#endif // G_ENCODING_GLSL
