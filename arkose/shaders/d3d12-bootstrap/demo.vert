layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec2 aTexCoord;

layout(set = 0, binding = 0) uniform restrict readonly ScaleBlock { vec4 scale; };

layout(location = 0) out vec2 vTexCoord;

void main()
{
    vTexCoord = aTexCoord;
    gl_Position = vec4(aPosition, 1.0);
    gl_Position.xy *= scale.xy;
}
