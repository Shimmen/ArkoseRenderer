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

    ASSERT(m_shadowMapSize.width() > 0 && m_shadowMapSize.height() > 0);
    Texture& shadowMap = scene()->registry().createTexture2D(m_shadowMapSize, Texture::Format::Depth32F, Texture::Filters::linear(), Texture::Mipmap::None, Texture::WrapModes::clampAllToEdge());
    m_shadowMap = &shadowMap;

    return shadowMap;
}

RenderTarget& Light::shadowMapRenderTarget()
{
    if (m_shadowMapRenderTarget)
        return *m_shadowMapRenderTarget;

    if (!scene())
        LogErrorAndExit("Light: can't request shadow map render target for light that is not part of a scene, exiting\n");

    RenderTarget& renderTarget = scene()->registry().createRenderTarget({ { RenderTarget::AttachmentType::Depth, &shadowMap() } });
    m_shadowMapRenderTarget = &renderTarget;

    return renderTarget;
}

RenderState& Light::getOrCreateCachedShadowMapRenderState(const std::string& cacheIdentifier, std::function<RenderState&(Registry& sceneRegistry)> creationCallback)
{
    if (!scene())
        LogErrorAndExit("Light: can't get or create shadow map render state for light that is not part of a scene, exiting\n");

    auto entry = m_cachedRenderStates.find(cacheIdentifier);
    if (entry != m_cachedRenderStates.end())
        return *entry->second;

    RenderState& newRenderState = creationCallback(scene()->registry());
    m_cachedRenderStates[cacheIdentifier] = &newRenderState;

    return newRenderState;
}
