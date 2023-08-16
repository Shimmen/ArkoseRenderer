#include "IconManager.h"

#include "core/Logging.h"
#include "rendering/backend/base/Backend.h"
#include <fmt/format.h>
#include <string_view>

IconManager::IconManager(Backend& backend)
{
    using namespace std::string_view_literals;

    m_lightbulbIcon = loadIcon(backend, "lightbulb-512"sv);
}

IconManager::~IconManager() = default;

Icon IconManager::loadIcon(Backend& backend, std::string_view iconName)
{
    std::string iconPath = fmt::format("assets/icons/{}.png", iconName);
    ImageAsset* imageAsset = ImageAsset::loadOrCreate(iconPath);

    if (imageAsset == nullptr) {
        ARKOSE_LOG_FATAL("Failed to load common icon '{}' (with path '{}')", iconName, iconPath);
    }

    Texture::Description desc { .type = Texture::Type::Texture2D,
                                .arrayCount = 1u,
                                .extent = { imageAsset->width(), imageAsset->height(), 1 },
                                .format = Texture::convertImageFormatToTextureFormat(imageAsset->format(), imageAsset->type()),
                                .filter = Texture::Filters::linear(),
                                .wrapMode = ImageWrapModes::repeatAll(),
                                .mipmap = Texture::Mipmap::None,
                                .multisampling = Texture::Multisampling::None };

    auto iconTexture = backend.createTexture(desc);
    iconTexture->setData(imageAsset->pixelDataForMip(0).data(), imageAsset->pixelDataForMip(0).size(), 0);

    std::string textureName = fmt::format("Icon<{}>", iconName);
    iconTexture->setName(textureName);

    return Icon(imageAsset, std::move(iconTexture));
}
