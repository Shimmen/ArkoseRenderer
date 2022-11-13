#include "TextureCompressor.h"

#include "asset/ImageAsset.h"
#include "core/Assert.h"
#include "core/Logging.h"
#include "utility/Profiling.h"
#include <fmt/format.h>
#include <rdo_bc_encoder.h>
#include <thread>

std::unique_ptr<ImageAsset> TextureCompressor::compressBC7(ImageAsset const& inputImage)
{
    SCOPED_PROFILE_ZONE();

    ARKOSE_ASSERT(inputImage.isUncompressed());
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
            ARKOSE_LOG(Error, "Failed to init BC7 encoder");
            return nullptr;
        }

        if (!encoder.encode()) {
            ARKOSE_LOG(Error, "Failed to BC7 encode image");
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

    return ImageAsset::createCopyWithReplacedFormat(inputImage, ImageFormat::BC7, std::move(compressedPixelData), std::move(compressedMips));
}
