#include "Light.h"

#include "rendering/Registry.h"
#include "rendering/scene/Scene.h"
#include "utility/Logging.h"

void Light::setShadowMapSize(Extent2D size)
{
    if (m_shadowMapSize == size)
        return;
    m_shadowMapSize = size;
    m_shadowMap = nullptr;
}

Texture& Light::shadowMap()
{
    if (m_shadowMap)
        return *m_shadowMap;

    if (!scene())
        LogErrorAndExit("Light: can't request shadow map for light that is not part of a scene, exiting\n");

    MOOSLIB_ASSERT(m_shadowMapSize.width() > 0 && m_shadowMapSize.height() > 0);
    Texture& shadowMap = scene()->registry().createTexture2D(m_shadowMapSize, Texture::Format::Depth32F);
    m_shadowMap = &shadowMap;

    return shadowMap;
}
