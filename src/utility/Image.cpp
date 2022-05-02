#include "Image.h"

#include "core/Assert.h"
#include "core/Logging.h"
#include "utility/FileIO.h"
#include "utility/Profiling.h"
#include <memory>
#include <moos/core.h>
#include <mutex>
#include <stb_image.h>
#include <unordered_map>

static std::unordered_map<std::string, Image::Info> s_infoCache {};
static std::mutex s_infoCacheMutex {};

static std::unordered_map<std::string, std::unique_ptr<Image>> s_imageCache {};
static std::mutex s_imageCacheMutex {};

Image::Info* Image::getInfo(const std::string& imagePath, bool quiet)
{
    SCOPED_PROFILE_ZONE();

    std::scoped_lock<std::mutex> lock { s_infoCacheMutex };

    auto entry = s_infoCache.find(imagePath);
    if (entry != s_infoCache.end())
        return &entry->second;

    FILE* file = fopen(imagePath.c_str(), "rb");
    if (file == nullptr) {
        if (!quiet)
            ARKOSE_LOG(Error, "Image: could not read file at path '{}', which is required for info.", imagePath);
        return nullptr;
    }

    Image::Info& info = s_infoCache[imagePath];

    int componentCount;
    stbi_info_from_file(file, &info.width, &info.height, &componentCount);

    ARKOSE_ASSERT(componentCount >= 1 && componentCount <= 4);
    info.pixelType = static_cast<PixelType>(componentCount);

    // FIXME: Add support for 16-bit images (e.g. some PNGs)
    info.componentType = stbi_is_hdr_from_file(file)
        ? ComponentType::Float
        : ComponentType::UInt8;

    return &info;
}

Image* Image::load(const std::string& imagePath, PixelType pixelType, bool skipReadableCheck)
{
    SCOPED_PROFILE_ZONE();

    {
        //SCOPED_PROFILE_ZONE_NAMED("Image cache mutex");
        std::scoped_lock<std::mutex> lock(s_imageCacheMutex);
        auto entry = s_imageCache.find(imagePath);
        if (entry != s_imageCache.end()) {
            Image* image = entry->second.get();
            // For now we only load RGBA images, but later we might wanna do some more advanced caching,
            //  where e.g. (path, RGBA) is loaded differently to a (path, RGB) (i.e., same path, different types)
            ARKOSE_ASSERT(image->info().pixelType == pixelType);
            return image;
        }
    }

    if (!skipReadableCheck) {
        if (!FileIO::isFileReadable(imagePath)) {
            ARKOSE_LOG(Fatal, "Image: could not read file at path '{}'.", imagePath);
        }
    }

    //ARKOSE_LOG(Info, "Image: actually loading texture '{}'", imagePath);

    FILE* file = fopen(imagePath.c_str(), "rb");
    ARKOSE_ASSERT(file);

    int desiredNumberOfComponents = static_cast<int>(pixelType);
    ARKOSE_ASSERT(desiredNumberOfComponents >= 1 && desiredNumberOfComponents <= 4);

    Info info;
    info.pixelType = pixelType;

    void* data;
    size_t size;

    if (stbi_is_hdr_from_file(file)) {
        info.componentType = ComponentType::Float;
        data = stbi_loadf_from_file(file, &info.width, &info.height, nullptr, desiredNumberOfComponents);
        size = info.width * info.height * desiredNumberOfComponents * sizeof(float);
    } else {
        info.componentType = ComponentType::UInt8;
        data = stbi_load(imagePath.c_str(), &info.width, &info.height, nullptr, desiredNumberOfComponents);
        size = info.width * info.height * desiredNumberOfComponents * sizeof(stbi_uc);
    }

    auto image = std::make_unique<Image>(MemoryType::EncodedImage, info, data, size);

    {
        //SCOPED_PROFILE_ZONE_NAMED("Image cache mutex");
        std::scoped_lock<std::mutex> lock(s_imageCacheMutex);
        s_imageCache[imagePath] = std::move(image);
        return s_imageCache[imagePath].get();
    }
}

Image::Image(MemoryType type, Info info, void* data, size_t size)
    : m_data(data)
    , m_size(size)
    , m_info(info)
    , m_type(type)
{
}
