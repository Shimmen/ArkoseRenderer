#include "Image.h"

#include "utility/FileIO.h"
#include "utility/Logging.h"
#include "utility/Profiling.h"
#include <memory>
#include <moos/core.h>
#include <stb_image.h>
#include <unordered_map>

static std::unordered_map<std::string, Image::Info> s_infoCache {};
static std::unordered_map<std::string, std::unique_ptr<Image>> s_imageCache {};

Image::Info* Image::getInfo(const std::string& imagePath, bool quiet)
{
    SCOPED_PROFILE_ZONE();

    auto entry = s_infoCache.find(imagePath);
    if (entry != s_infoCache.end())
        return &entry->second;

    // TODO: Consider putting like a nullptr Image::Info in the cache in this case?
    if (!FileIO::isFileReadable(imagePath)) {
        if (!quiet) {
            LogError("Image: could not read file at path '%s', which is required for info.\n", imagePath.c_str());
        }
        return nullptr;
    }

    FILE* file = fopen(imagePath.c_str(), "rb");
    ASSERT(file);

    Image::Info& info = s_infoCache[imagePath];

    int componentCount;
    stbi_info_from_file(file, &info.width, &info.height, &componentCount);

    ASSERT(componentCount >= 1 && componentCount <= 4);
    info.pixelType = static_cast<PixelType>(componentCount);

    // FIXME: Add support for 16-bit images (e.g. some PNGs)
    info.componentType = stbi_is_hdr_from_file(file)
        ? ComponentType::Float
        : ComponentType::UInt8;

    return &info;
}

Image* Image::load(const std::string& imagePath, PixelType pixelType)
{
    SCOPED_PROFILE_ZONE();

    auto entry = s_imageCache.find(imagePath);
    if (entry != s_imageCache.end()) {
        Image* image = entry->second.get();
        // For now we only load RGBA images, but later we might wanna do some more advanced caching,
        //  where e.g. (path, RGBA) is loaded differently to a (path, RGB) (i.e., same path, different types)
        ASSERT(image->info().pixelType == pixelType);
        return image;
    }

    // TODO: Consider putting like a nullptr Image::Info in the cache in this case?
    if (!FileIO::isFileReadable(imagePath))
        LogErrorAndExit("Image: could not read file at path '%s'.\n", imagePath.c_str());

    //LogInfo("Image: actually loading texture '%s'\n", imagePath.c_str());

    FILE* file = fopen(imagePath.c_str(), "rb");
    ASSERT(file);

    int desiredNumberOfComponents = static_cast<int>(pixelType);
    ASSERT(desiredNumberOfComponents >= 1 && desiredNumberOfComponents <= 4);

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

    auto image = std::make_unique<Image>(DataOwner::StbImage, info, data, size);
    s_imageCache[imagePath] = std::move(image);

    return s_imageCache[imagePath].get();
}

Image::Image(DataOwner owner, Info info, void* data, size_t size)
    : m_data(data)
    , m_size(size)
    , m_info(info)
    , m_owner(owner)
{
}

Image::~Image()
{
    switch (m_owner) {
    case Image::DataOwner::External:
        return;
    case Image::DataOwner::StbImage:
        if (m_data != nullptr && m_size > 0)
            stbi_image_free(m_data);
        return;
    default:
        ASSERT_NOT_REACHED();
        return;
    }
}
