layout(location = 0) in vec2 vTexCoord;

layout(set = 0, binding = 1) uniform sampler2D uTexture;

layout(location = 0) out vec4 oColor;

void main()
{
    oColor = texture(uTexture, vTexCoord);
}
