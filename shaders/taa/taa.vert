#version 460

layout(location = 0) in vec2 aPosition;

layout(location = 0) noperspective out vec2 vTexCoord;

void main()
{
    vTexCoord = aPosition * 0.5 + 0.5;
    gl_Position = vec4(aPosition, 0.0, 1.0);
}
