#pragma once

#include "core/Types.h"
#include "asset/ImageAsset.h"
#include <vector>

namespace DDS {
    // Reading
    bool isValidHeader(u8 const* data, size_t size);
    u8 const* loadFromMemory(u8 const* data, size_t size, Extent3D& outExtent, ImageFormat& outFormat, bool& outSrgb, u32& outNumMips);
    std::vector<ImageMip> computeMipOffsetAndSize(Extent3D extentMip0, ImageFormat format, u32 numMips);

    // Writing
    bool writeToFile(std::string_view filePath, u8 const* data, size_t size, Extent3D extent, ImageFormat format, u32 numMips);
}
