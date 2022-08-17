#include "TextureCompressor.h"

#include "core/Assert.h"
#include "core/Logging.h"
#include "utility/Image.h"
#include "utility/Profiling.h"
#include <rdo_bc_encoder.h>
#include <thread>

std::unique_ptr<Image> TextureCompressor::compressBC7(Image const& inputImage)
{
    SCOPED_PROFILE_ZONE();

    const Image::Info& sourceInfo = inputImage.info();
    ARKOSE_ASSERT(sourceInfo.width > 0 && sourceInfo.height > 0);
    ARKOSE_ASSERT(sourceInfo.pixelType == Image::PixelType::RGBA); // TODO: Also add support for RGB, which will require some manual padding
    ARKOSE_ASSERT(sourceInfo.componentType == Image::ComponentType::UInt8);

    // Create an image that can be used by the encoder
    utils::image_u8 sourceImage { static_cast<uint32_t>(sourceInfo.width), static_cast<uint32_t>(sourceInfo.height) };
    std::memcpy(sourceImage.get_pixels().data(), inputImage.data(), inputImage.dataSize());

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
    std::vector<uint8_t> compressedData {};
    compressedData.resize(compressedSize);
    std::memcpy(compressedData.data(), encoder.get_blocks(), compressedSize);

    Image::Info compressedImageInfo = sourceInfo;
    compressedImageInfo.compressionType = Image::CompressionType::BC7;

    return std::make_unique<Image>(compressedImageInfo, compressedData);
}
