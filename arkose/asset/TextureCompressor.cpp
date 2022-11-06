#include "TextureCompressor.h"

#include "asset/ImageAsset.h"
#include "core/Assert.h"
#include "core/Logging.h"
#include "utility/Profiling.h"
#include <rdo_bc_encoder.h>
#include <thread>

std::unique_ptr<ImageAsset> TextureCompressor::compressBC7(ImageAsset const& inputImage)
{
    SCOPED_PROFILE_ZONE();

    ARKOSE_ASSERT(inputImage.isUncompressed());
    ARKOSE_ASSERT(inputImage.width() > 0 && inputImage.height() > 0 && inputImage.depth() == 1);
    ARKOSE_ASSERT(inputImage.width() % 4 == 0 && inputImage.height() % 4 == 0);
    ARKOSE_ASSERT(inputImage.format() == ImageFormat::RGBA8); // TODO: Also add support for RGB, which will require some manual padding

    // Create an image that can be used by the encoder
    utils::image_u8 sourceImage { inputImage.width(), inputImage.height() };
    std::memcpy(sourceImage.get_pixels().data(), inputImage.pixelData().data(), inputImage.pixelData().size());

    rdo_bc::rdo_bc_params params {};
    params.m_status_output = false; // no debug printing
    params.m_use_bc7e = false; // no ispc support yet (on our end)

    params.m_dxgi_format = DXGI_FORMAT_BC7_UNORM;

    // RDO (optional; will decrease compressed size on disc, but with some slight sacrifice to quality)
    //params.m_rdo_max_threads = std::max(1u, std::thread::hardware_concurrency());
    //params.m_bc7enc_reduce_entropy = true;
    //params.m_rdo_lambda = 100.0f;

    rdo_bc::rdo_bc_encoder encoder {};

    if (!encoder.init(sourceImage, params)) {
        ARKOSE_LOG(Error, "Failed to init BC7 encoder");
        return nullptr;
    }

    if (!encoder.encode()) {
        ARKOSE_LOG(Error, "Failed to BC7 encode image");
        return nullptr;
    }

    uint32_t compressedSize = encoder.get_total_blocks_size_in_bytes();
    uint8_t const* compressedData = static_cast<uint8_t const*>(encoder.get_blocks());

    return ImageAsset::createCopyWithReplacedFormat(inputImage, ImageFormat::BC7, compressedData, compressedSize);
}
