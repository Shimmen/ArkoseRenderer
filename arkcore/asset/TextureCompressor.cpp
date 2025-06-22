#include "TextureCompressor.h"

#include "asset/ImageAsset.h"
#include "core/Assert.h"
#include "core/Logging.h"
#include "utility/Profiling.h"
#include <fmt/format.h>
#include <rdo_bc_encoder.h>
#include <bc7decomp.h>
#include <thread>

static std::unique_ptr<ImageAsset> compressWithParams(ImageAsset const& inputImage, ImageFormat compressedFormat, rdo_bc::rdo_bc_params params)
{
    std::vector<u8> compressedPixelData {};
    std::vector<ImageMip> compressedMips {};

    for (size_t mipIdx = 0; mipIdx < inputImage.numMips(); ++mipIdx) {

        std::string zoneName = fmt::format("Mip level {}", mipIdx);
        SCOPED_PROFILE_ZONE_DYNAMIC(zoneName, 0xaa5577);

        auto pixelData = inputImage.pixelDataForMip(mipIdx);

        // Create an image that can be used by the encoder
        Extent3D mipExtent = inputImage.extentAtMip(mipIdx);
        utils::image_u8 sourceImage { mipExtent.width(), mipExtent.height() };
        std::memcpy(sourceImage.get_pixels().data(), pixelData.data(), pixelData.size());

        rdo_bc::rdo_bc_encoder encoder {};

        if (!encoder.init(sourceImage, params)) {
            ARKOSE_LOG(Error, "Failed to init encoder");
            return nullptr;
        }

        if (!encoder.encode()) {
            ARKOSE_LOG(Error, "Failed to encode/compress image");
            return nullptr;
        }

        u32 compressedMipSize = encoder.get_total_blocks_size_in_bytes();
        u8 const* compressedMipData = static_cast<u8 const*>(encoder.get_blocks());

        size_t currentOffset = compressedMips.size() > 0 ? (compressedMips.back().offset + compressedMips.back().size) : 0;
        compressedPixelData.resize(currentOffset + compressedMipSize);

        std::memcpy(compressedPixelData.data() + currentOffset, compressedMipData, compressedMipSize);

        compressedMips.push_back(ImageMip { .offset = currentOffset,
                                            .size = compressedMipSize });
    }

    return ImageAsset::createCopyWithReplacedFormat(inputImage, compressedFormat, std::move(compressedPixelData), std::move(compressedMips));

}

std::unique_ptr<ImageAsset> TextureCompressor::compressBC7(ImageAsset const& inputImage)
{
    SCOPED_PROFILE_ZONE();

    ARKOSE_ASSERT(inputImage.width() > 0 && inputImage.height() > 0 && inputImage.depth() == 1);
    ARKOSE_ASSERT(inputImage.width() % 4 == 0 && inputImage.height() % 4 == 0);
    ARKOSE_ASSERT(inputImage.format() == ImageFormat::RGBA8); // TODO: Also add support for RGB, which will require some manual padding

    rdo_bc::rdo_bc_params params {};
    params.m_status_output = false; // no debug printing
    params.m_use_bc7e = false; // no ispc support yet (on our end)

    params.m_dxgi_format = DXGI_FORMAT_BC7_UNORM;

    // RDO (optional; will decrease compressed size on disc, but with some slight sacrifice to quality)
    //params.m_rdo_max_threads = std::max(1u, std::thread::hardware_concurrency());
    //params.m_bc7enc_reduce_entropy = true;
    //params.m_rdo_lambda = 100.0f;

    return compressWithParams(inputImage, ImageFormat::BC7, params);
}

std::unique_ptr<ImageAsset> TextureCompressor::compressBC5(ImageAsset const& inputImage)
{
    SCOPED_PROFILE_ZONE();

    ARKOSE_ASSERT(inputImage.width() > 0 && inputImage.height() > 0 && inputImage.depth() == 1);
    ARKOSE_ASSERT(inputImage.width() % 4 == 0 && inputImage.height() % 4 == 0);

    // TODO: Also add support for RB and RGB, but the encoder expects a 4-component image input even for BC5
    ARKOSE_ASSERT(inputImage.format() == ImageFormat::RGBA8);

    rdo_bc::rdo_bc_params params {};
    params.m_dxgi_format = DXGI_FORMAT_BC5_UNORM;
    params.m_status_output = false; // no debug printing
    params.m_use_hq_bc345 = true;

    return compressWithParams(inputImage, ImageFormat::BC5, params);
}

std::unique_ptr<ImageAsset> TextureCompressor::decompressToRGBA32F(ImageAsset const& compressedImage)
{
    SCOPED_PROFILE_ZONE();

    ARKOSE_ASSERT(compressedImage.hasCompressedFormat());

    if (compressedImage.format() != ImageFormat::BC7) {
        ARKOSE_LOG(Error, "Unsupported compressed format for decompression: {} (TODO: Implement!)", compressedImage.format());
        return nullptr;
    }

    std::vector<ImageMip> mips {};
    std::vector<u8> pixelData {};

    for (size_t mipIdx = 0; mipIdx < compressedImage.numMips(); ++mipIdx) {
        // Get the compressed mip data
        auto const& compressedMipData = compressedImage.pixelDataForMip(mipIdx);
        auto extent = compressedImage.extentAtMip(mipIdx);

        // Calculate the size of the uncompressed mip
        size_t uncompressedMipSize = extent.width() * extent.height() * 4 * sizeof(float);
        size_t currentOffset = pixelData.size();
        pixelData.resize(currentOffset + uncompressedMipSize);

        // Create the target buffer for this mip level
        float* outputData = reinterpret_cast<float*>(pixelData.data() + currentOffset);

        switch (compressedImage.format()) {
        case ImageFormat::BC7: {
            // Calculate the number of blocks in each dimension
            // BC7 uses 4x4 pixel blocks
            uint32_t blocksWide = (extent.width() + 3) / 4;
            uint32_t blocksHigh = (extent.height() + 3) / 4;

            for (uint32_t blockY = 0; blockY < blocksHigh; ++blockY) {
                for (uint32_t blockX = 0; blockX < blocksWide; ++blockX) {
                    // Calculate block index and pointer to compressed block data
                    size_t blockIdx = blockY * blocksWide + blockX;
                    const void* blockData = compressedMipData.data() + (blockIdx * 16); // 16 bytes per block

                    // Temporary buffer for the decompressed 4x4 pixel block
                    bc7decomp::color_rgba decompressedBlock[16];

                    // Decompress this block
                    bc7decomp::unpack_bc7(blockData, decompressedBlock);

                    // Copy the decompressed pixels to the output buffer, converting to float
                    for (uint32_t y = 0; y < 4; ++y) {
                        for (uint32_t x = 0; x < 4; ++x) {
                            // Calculate the pixel position in the output image
                            uint32_t pixelX = blockX * 4 + x;
                            uint32_t pixelY = blockY * 4 + y;

                            // Skip pixels outside the actual dimensions
                            if (pixelX >= extent.width() || pixelY >= extent.height()) {
                                continue;
                            }

                            // Calculate the position in the output array
                            size_t outputIdx = (pixelY * extent.width() + pixelX) * 4;

                            // Get the decompressed pixel
                            bc7decomp::color_rgba const& pixel = decompressedBlock[y * 4 + x];

                            // Convert to float (0.0 to 1.0)
                            outputData[outputIdx + 0] = pixel.r / 255.0f;
                            outputData[outputIdx + 1] = pixel.g / 255.0f;
                            outputData[outputIdx + 2] = pixel.b / 255.0f;
                            outputData[outputIdx + 3] = pixel.a / 255.0f;
                        }
                    }
                }
            }
            break;
        }
        case ImageFormat::BC5: {
            // TODO: Handle BC5 decompression
            break;
        }
        default:
            ASSERT_NOT_REACHED();
        }

        // Add the mip info
        mips.push_back(ImageMip { .offset = currentOffset, .size = uncompressedMipSize });
    }

    return ImageAsset::createCopyWithReplacedFormat(compressedImage, ImageFormat::RGBA32F, std::move(pixelData), std::move(mips));
}
