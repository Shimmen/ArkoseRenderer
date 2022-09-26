#include "asset/ImageAsset.h"
#include "rendering/backend/base/Texture.h"

namespace AssetTypes {
vec2 convert(Arkose::Asset::Vec2);
vec3 convert(Arkose::Asset::Vec3);
vec4 convert(Arkose::Asset::Vec4);
vec4 convert(Arkose::Asset::ColorRGBA);

Texture::Format convertFormat(ImageFormat, ColorSpace, bool sRGBoverride);
Texture::MinFilter convertMinFilter(ImageFilter);
Texture::MagFilter convertMagFilter(ImageFilter);
Texture::Mipmap convertMipFilter(ImageFilter, bool useMipmap);
Texture::WrapMode convertWrapMode(WrapMode);
}

#include "AssetTypes.inl"
