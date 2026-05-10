#version 460

layout(location = 0) flat in uint vDrawableIdx;
layout(location = 0) out uint oDrawableIdx;

void main()
{
    oDrawableIdx = vDrawableIdx;
}
