#include "asset/ImageAsset.h"
#include "rendering/backend/base/Texture.h"

namespace AssetTypes {
inline Texture::Format convertFormat(ImageFormat, ColorSpace, bool sRGBoverride);
inline Texture::MinFilter convertMinFilter(ImageFilter);
inline Texture::MagFilter convertMagFilter(ImageFilter);
inline Texture::Mipmap convertMipFilter(ImageFilter, bool useMipmap);
}

#include "AssetTypes.inl"
