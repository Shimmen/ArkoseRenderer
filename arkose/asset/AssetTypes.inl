#include <ark/vector.h>

namespace AssetTypes {

vec2 convert(Arkose::Asset::Vec2 v)
{
    return vec2(v.x(), v.y());
}

vec3 convert(Arkose::Asset::Vec3 v)
{
    return vec3(v.x(), v.y(), v.z());
}

vec4 convert(Arkose::Asset::Vec4 v)
{
    return vec4(v.x(), v.y(), v.z(), v.w());
}

vec4 convert(Arkose::Asset::ColorRGBA c)
{
    return vec4(c.r(), c.g(), c.b(), c.a());
}

}
