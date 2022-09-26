#include "asset/ImageAsset.h"
#include "rendering/backend/base/Texture.h"

namespace AssetTypes {
inline vec2 convert(Arkose::Asset::Vec2);
inline vec3 convert(Arkose::Asset::Vec3);
inline vec4 convert(Arkose::Asset::Vec4);
inline vec4 convertColorRGBA(Arkose::Asset::ColorRGBA);

inline Arkose::Asset::Vec2 convert(vec2);
inline Arkose::Asset::Vec3 convert(vec3);
inline Arkose::Asset::Vec4 convert(vec4);
inline Arkose::Asset::ColorRGBA convertColorRGBA(vec4);

inline Texture::Format convertFormat(ImageFormat, ColorSpace, bool sRGBoverride);
inline Texture::MinFilter convertMinFilter(ImageFilter);
inline Texture::MagFilter convertMagFilter(ImageFilter);
inline Texture::Mipmap convertMipFilter(ImageFilter, bool useMipmap);
inline Texture::WrapMode convertWrapMode(WrapMode);
}

#include "AssetTypes.inl"
