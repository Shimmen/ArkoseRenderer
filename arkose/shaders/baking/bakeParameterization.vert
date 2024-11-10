#version 460

layout(location = 0) in vec2 aTexCoord;

void main()
{
    // NOTE: This only works if we assume there's no uv wrapping & no overlap between triangles in the parameterization!
    //       This is likely not true for a bunch of meshes (hence having a separate texcoord0 and texcoord1 is common).
    // NOTE 2: In some cases there's wrapping but still no overlap, so let's simply support that with fract().
    vec2 parameterizationCoords = fract(aTexCoord.xy);

    vec2 normalizedDeviceCoords = parameterizationCoords * vec2(2.0) - vec2(1.0);
    gl_Position = vec4(normalizedDeviceCoords, 0.0, 1.0);
}
