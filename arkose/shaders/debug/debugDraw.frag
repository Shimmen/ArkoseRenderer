#version 460

layout(location = 0) in vec3 vColor;
#if WITH_TEXTURES
layout(location = 1) in vec2 vTexCoord;
#endif

#if WITH_TEXTURES
layout(set = 1, binding = 0) uniform sampler2D colorTexture;
#endif

layout(location = 0) out vec3 oColor;

void main()
{
    oColor = vColor;
#if WITH_TEXTURES
    vec4 texData = texture(colorTexture, vTexCoord);
    oColor *= texData.rgb;
    if (texData.a < 0.2) {
        discard;
    }
#endif
}
