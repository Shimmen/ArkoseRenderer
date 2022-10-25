#version 460

layout(location = 0) in vec3 vColor;

layout(location = 0) out vec3 oColor;

void main()
{
    oColor = vColor;
}
