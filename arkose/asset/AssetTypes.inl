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

Texture::Format convertFormat(ImageFormat format, ColorSpace colorSpace, bool sRGBoverride)
{
    // TODO: Respect the color space of the source texture!
    bool sRGB = sRGBoverride; // (colorSpace == ColorSpace::sRGB_encoded);
    switch (format) {
    case ImageFormat::RGBA8:
        return sRGB ? Texture::Format::sRGBA8 : Texture::Format::RGBA8;
    default:
        ASSERT_NOT_REACHED();
        return Texture::Format::Unknown;
    }
}

Texture::MinFilter convertMinFilter(ImageFilter minFilter)
{
    switch (minFilter) {
    case ImageFilter::Nearest:
        return Texture::MinFilter::Nearest;
    case ImageFilter::Linear:
        return Texture::MinFilter::Linear;
    default:
        ASSERT_NOT_REACHED();
    }
}

Texture::MagFilter convertMagFilter(ImageFilter magFilter)
{
    switch (magFilter) {
    case ImageFilter::Nearest:
        return Texture::MagFilter::Nearest;
    case ImageFilter::Linear:
        return Texture::MagFilter::Linear;
    default:
        ASSERT_NOT_REACHED();
    }
}

Texture::Mipmap convertMipFilter(ImageFilter mipFilter, bool useMipmap)
{
    if (useMipmap) {
        switch (mipFilter) {
        case ImageFilter::Nearest:
            return Texture::Mipmap::Nearest;
        case ImageFilter::Linear:
            return Texture::Mipmap::Linear;
        default:
            ASSERT_NOT_REACHED();
        }
    } else {
        return Texture::Mipmap::None;
    }
}

Texture::WrapMode convertWrapMode(WrapMode wrapMode)
{
    switch (wrapMode) {
    case WrapMode::Repeat:
        return Texture::WrapMode::Repeat;
    case WrapMode::MirroredRepeat:
        return Texture::WrapMode::MirroredRepeat;
    case WrapMode::ClampToEdge:
        return Texture::WrapMode::ClampToEdge;
    default:
        ASSERT_NOT_REACHED();
    }
}

}
