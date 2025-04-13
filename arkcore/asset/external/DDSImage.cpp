#include "DDSImage.h"

struct DDSHeader {
    // https://learn.microsoft.com/en-us/windows/win32/direct3d11/texture-block-compression-in-direct3d-11
    // `dxgiformat.h` is used as reference, however not included here, as that header is not available on all platforms.

    static constexpr u32 DDS_MAGIC = 0x20534444;

    static constexpr u32 FOURCC_R32F = 0x72;
    static constexpr u32 FOURCC_DXT1 = 0x31545844;
    static constexpr u32 FOURCC_DXT3 = 0x33545844;
    static constexpr u32 FOURCC_DXT5 = 0x35545844;
    static constexpr u32 FOURCC_DX10 = 0x30315844;

    static constexpr u32 DDSD_CAPS = 0x1;
    static constexpr u32 DDSD_HEIGHT = 0x2;
    static constexpr u32 DDSD_WIDTH = 0x4;
    static constexpr u32 DDSD_PITCH = 0x8;
    static constexpr u32 DDSD_PIXELFORMAT = 0x1000;
    static constexpr u32 DDSD_MIPMAPCOUNT = 0x20000;
    static constexpr u32 DDSD_LINEARSIZE = 0x80000;
    static constexpr u32 DDSD_DEPTH = 0x800000;

    static constexpr u32 DDPF_ALPHAPIXELS = 0x1;
    static constexpr u32 DDPF_FOURCC = 0x4;
    static constexpr u32 DDPF_RGB = 0x40;

    u32 size;
    u32 flags;
    u32 height;
    u32 width;
    u32 pitchOrLinearSize;
    u32 depth;
    u32 mipMapCount;
    u32 reserved1[11];

    struct PixelFormat {
        u32 size;
        u32 flags;
        u32 fourCC;
        u32 rgbBitCount;
        u32 rBitMask;
        u32 gBitMask;
        u32 bBitMask;
        u32 aBitMask;
    } pixelFormat;

    u32 caps;
    u32 caps2;
    u32 caps3;
    u32 caps4;
    u32 reserved2;
};

struct DDSHeaderDX10 {
    u32 dxgiFormat;
    u32 resourceDimension;
    u32 miscFlag;
    u32 arraySize;
    u32 miscFlags2;
};

bool DDS::isValidHeader(u8 const* data, size_t size)
{
    if (size < 128) {
        return false;
    }

    u32 magic = *reinterpret_cast<u32 const*>(data);
    if (magic != DDSHeader::DDS_MAGIC) {
        return false;
    }

    DDSHeader const* header = reinterpret_cast<DDSHeader const*>(data + 4);
    if (header->size != 124) {
        return false;
    }

    return true;
}

u8 const* DDS::loadFromMemory(u8 const* data, size_t size, Extent3D& outExtent, ImageFormat& outFormat, bool& outSrgb, u32& outNumMips)
{
    outExtent = Extent3D { 0, 0, 0 };
    outFormat = ImageFormat::Unknown;
    outSrgb = false;
    outNumMips = 0;

    if (!isValidHeader(data, size)) {
        return nullptr;
    }

    DDSHeader const* header = reinterpret_cast<DDSHeader const*>(data + 4);
    u8 const* dataStart = reinterpret_cast<u8 const*>(header) + 124;

    u32 width = 0;
    if (header->flags & DDSHeader::DDSD_WIDTH) {
        width = header->width;
    } else {
        return nullptr;
    }

    u32 height = 0;
    if (header->flags & DDSHeader::DDSD_HEIGHT) {
        height = header->height;
    } else {
        return nullptr;
    }

    u32 depth = 1;
    if (header->flags & DDSHeader::DDSD_DEPTH) {
        depth = header->depth;
    }

    if (header->flags & DDSHeader::DDSD_MIPMAPCOUNT) {
        outNumMips = header->mipMapCount;
    } else {
        outNumMips = 1;
    }

    if (header->flags & DDSHeader::DDSD_PIXELFORMAT) {
        if (header->pixelFormat.flags & DDSHeader::DDPF_FOURCC) {
            switch (header->pixelFormat.fourCC) {
            case DDSHeader::FOURCC_DXT1:
                NOT_YET_IMPLEMENTED();
                //outFormat = ImageFormat::BC1;
                break;
            case DDSHeader::FOURCC_DXT3:
                NOT_YET_IMPLEMENTED();
                //outFormat = ImageFormat::BC2;
                break;
            case DDSHeader::FOURCC_DXT5:
                NOT_YET_IMPLEMENTED();
                //outFormat = ImageFormat::BC3;
                break;
            case DDSHeader::FOURCC_DX10: {
                ARKOSE_ASSERT(size >= 4 + sizeof(DDSHeader) + sizeof(DDSHeaderDX10));
                DDSHeaderDX10 const* headerDX10 = reinterpret_cast<DDSHeaderDX10 const*>(dataStart);
                dataStart += 20; // skip past the DX10 header

                switch (headerDX10->dxgiFormat) {
                case 98: // DXGI_FORMAT_BC7_UNORM
                    outFormat = ImageFormat::BC7;
                    outSrgb = false;
                    break;
                case 99: // DXGI_FORMAT_BC7_UNORM_SRGB
                    outFormat = ImageFormat::BC7;
                    outSrgb = true;
                    break;
                default:
                    NOT_YET_IMPLEMENTED();
                    break;
                }
            } break;
            default:
                ASSERT_NOT_REACHED();
            }
        } else if (header->pixelFormat.flags & DDSHeader::DDPF_RGB) {
            if (header->pixelFormat.flags & DDSHeader::DDPF_ALPHAPIXELS) {
                outFormat = ImageFormat::RGBA8;
            } else {
                outFormat = ImageFormat::RGB8;
            }
        } else {
            ASSERT_NOT_REACHED();
        }
    } else {
        ASSERT_NOT_REACHED();
    }

    outExtent = Extent3D { width, height, depth };
    return dataStart;
}

std::vector<ImageMip> DDS::computeMipOffsetAndSize(Extent3D extentMip0, ImageFormat format, u32 numMips)
{
    if (imageFormatIsBlockCompressed(format)) {
        ARKOSE_ASSERT(extentMip0.depth() == 1);
    }

    std::vector<ImageMip> mips;

    u32 currentOffset = 0;
    Extent3D currentExtent = extentMip0;
    for (u32 mipIdx = 0; mipIdx < numMips; ++mipIdx) {

        u32 mipSize;
        if (imageFormatIsBlockCompressed(format)) {

            #if 1
            // Assume the DDS file has properly padded these mips to 4x4 blocks
            u32 paddingX = currentExtent.width() % 4;
            u32 paddingY = currentExtent.height() % 4;
            if (paddingX > 0 || paddingY > 0) {
                currentExtent = { currentExtent.width() + paddingX,
                                  currentExtent.height() + paddingY,
                                  1 };
            }
            #else
            if (currentExtent.width() < 4 || currentExtent.height() < 4) {
                // Assume the DDS file has properly padded these mips to 4x4
                currentExtent = { std::max(4u, currentExtent.width()),
                                  std::max(4u, currentExtent.height()),
                                  1 };
            } else {
                ARKOSE_ASSERT(currentExtent.width() % 4 == 0);
                ARKOSE_ASSERT(currentExtent.height() % 4 == 0);
            }
            #endif

            u32 blocksX = currentExtent.width() / 4;
            u32 blocksY = currentExtent.height() / 4;
            u32 blockSize = imageFormatBlockSize(format);

            mipSize = blocksX * blocksY * blockSize;

        } else {

            u32 bytesPerPixel;
            switch (format) {
            case ImageFormat::RGB8:
                bytesPerPixel = 3;
                break;
            case ImageFormat::RGBA8:
                bytesPerPixel = 4;
                break;
            default:
                // well, also a bunch of not-yet-implemented formats here..
                ASSERT_NOT_REACHED();
            }

            mipSize = currentExtent.width() * currentExtent.height() * currentExtent.depth() * bytesPerPixel;
        }

        mips.push_back(ImageMip { .offset = currentOffset,
                                  .size = mipSize });

        currentOffset += mipSize;

        // Figure out the extents of the next mip
        currentExtent = Extent3D::divideAndRoundDownClampTo1(currentExtent, 2);
    }

    return mips;
}

bool DDS::writeToFile(std::filesystem::path const& filePath, u8 const* imageData, size_t imageDataSize, Extent3D extent, ImageFormat format, u32 numMips)
{
    size_t fileSize = 4 + sizeof(DDSHeader) + imageDataSize;

    bool hasDX10Header = false;
    if (imageFormatIsBlockCompressed(format)) {
        fileSize += sizeof(DDSHeaderDX10);
        hasDX10Header = true;

        ARKOSE_LOG(Error, "We can't yet handle the DX10 header when writing!");
        return false;
    }

    std::byte* fileData = static_cast<std::byte*>(malloc(fileSize));
    if (!fileData) {
        ARKOSE_LOG(Error, "Failed to allocate memory");
        return false;
    }

    u32* magic = reinterpret_cast<u32*>(fileData);
    *magic = DDSHeader::DDS_MAGIC;

    DDSHeader* header = reinterpret_cast<DDSHeader*>(fileData + 4);
    memset(header, 0, sizeof(DDSHeader));

    header->flags |= DDSHeader::DDSD_WIDTH;
    header->width = extent.width();

    header->flags |= DDSHeader::DDSD_HEIGHT;
    header->height = extent.height();

    if (extent.depth() > 1) {
        header->flags |= DDSHeader::DDSD_DEPTH;
        header->depth = extent.depth();
    }

    if (numMips > 1) {
        header->flags |= DDSHeader::DDSD_MIPMAPCOUNT;
        header->mipMapCount = numMips;
    }

    header->flags |= DDSHeader::DDSD_PIXELFORMAT;
    switch (format) {
    case ImageFormat::RGB8:
        header->pixelFormat.flags |= DDSHeader::DDPF_RGB;
        break;
    case ImageFormat::RGBA8:
        header->pixelFormat.flags |= DDSHeader::DDPF_RGB;
        header->pixelFormat.flags |= DDSHeader::DDPF_ALPHAPIXELS;
        break;
    case ImageFormat::R32F:
        header->pixelFormat.flags |= DDSHeader::DDPF_FOURCC;
        header->pixelFormat.fourCC |= DDSHeader::FOURCC_R32F;
        break;
    default:
        NOT_YET_IMPLEMENTED();
    }

    std::byte* imageDataStart = fileData + 4 + sizeof(DDSHeader) + (hasDX10Header ? sizeof(DDSHeaderDX10) : 0);
    memcpy(imageDataStart, imageData, imageDataSize);

    // TODO: Add error handling for writing files!
    FileIO::writeBinaryDataToFile(filePath, fileData, fileSize);

    return true;
}
