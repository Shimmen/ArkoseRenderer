#version 460

layout(location = 0) flat in uint vIndex;
layout(location = 0) out uint oIndex;

void main()
{
    oIndex = vIndex;
}
