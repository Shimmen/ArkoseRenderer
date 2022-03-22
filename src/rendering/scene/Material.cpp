#include "Material.h"

#include "rendering/Registry.h"
#include "rendering/scene/Mesh.h"
#include "rendering/scene/Model.h"
#include "utility/Logging.h"

bool Material::TextureDescription::operator==(const TextureDescription& other) const
{
    if (hasPath() && path != other.path)
        return false;
    if (hasImage() && image != other.image)
        return false;
    if (moos::dot(fallbackColor, other.fallbackColor) > 1e-3f)
        return false;
    return sRGB == other.sRGB && mipmapped == other.mipmapped && wrapMode == other.wrapMode && filters == other.filters;
}
