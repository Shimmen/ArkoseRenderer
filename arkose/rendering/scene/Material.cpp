#include "Material.h"

#include "rendering/Registry.h"
#include "rendering/scene/Mesh.h"
#include "rendering/scene/Model.h"

bool Material::TextureDescription::operator==(const TextureDescription& other) const
{
    if (hasPath() && path != other.path)
        return false;
    if (hasImage() && image != other.image)
        return false;
    // TODO: Use moos::distance (but it's not currently available for vec4.. instead now we just check max axis distance)
    vec4 diff = fallbackColor - other.fallbackColor;
    constexpr float maxDiff = 1e-3f;
    if (diff.x > maxDiff || diff.y > maxDiff || diff.z > maxDiff || diff.w > maxDiff)
        return false;
    return sRGB == other.sRGB && mipmapped == other.mipmapped && wrapMode == other.wrapMode && filters == other.filters;
}

std::string Material::TextureDescription::toString() const
{
    std::string result {};

    if (hasPath()) {
        result = fmt::format("Path '{}'", path);
    } else if (hasImage()) {
        const Image::Info& info = image.value().info();
        result = fmt::format("Image {}x{} (components: {}) ({})", info.width, info.height, static_cast<int>(info.pixelType), info.isHdr() ? "HDR" : "LDR");
    } else {
        result = fmt::format("PixelColor rgba({}, {}, {}, {})", fallbackColor.x, fallbackColor.y, fallbackColor.z, fallbackColor.w);
    }

    if (sRGB) {
        result += " sRGB";
    }

    if (mipmapped) {
        result += " with mipmaps";
    }

    auto wrapModeToString = [](Texture::WrapMode wrapMode) -> const char* {
        switch (wrapMode) {
        case Texture::WrapMode::Repeat:
            return "Repeat";
        case Texture::WrapMode::MirroredRepeat:
            return "MirroredRepeat";
        case Texture::WrapMode::ClampToEdge:
            return "ClampToEdge";
        default:
            ASSERT_NOT_REACHED();
        }
    };

    result += fmt::format(", wrap modes: ({}, {}, {})", wrapModeToString(wrapMode.u), wrapModeToString(wrapMode.v), wrapModeToString(wrapMode.w));

    auto magFilterToString = [](Texture::MagFilter magFilter) -> const char* {
        switch (magFilter) {
        case Texture::MagFilter::Linear:
            return "Linear";
        case Texture::MagFilter::Nearest:
            return "Nearest";
        default:
            ASSERT_NOT_REACHED();
        }
    };

    auto minFilterToString = [](Texture::MinFilter minFilter) -> const char* {
        switch (minFilter) {
        case Texture::MinFilter::Linear:
            return "Linear";
        case Texture::MinFilter::Nearest:
            return "Nearest";
        default:
            ASSERT_NOT_REACHED();
        }
    };

    result += fmt::format(", mag filter: {}", magFilterToString(filters.mag));
    result += fmt::format(", min filter: {}", minFilterToString(filters.min));

    return result;
}
