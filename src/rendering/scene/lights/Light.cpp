#include "Light.h"

#include "rendering/Registry.h"
#include "rendering/scene/Scene.h"
#include "utility/Logging.h"
#include "utility/Profiling.h"

void Light::setShadowMapSize(Extent2D size)
{
    if (m_shadowMapSize == size)
        return;
    m_shadowMapSize = size;
    m_shadowMap = nullptr;
}

Texture& Light::shadowMap()
{
    SCOPED_PROFILE_ZONE();

    if (m_shadowMap)
        return *m_shadowMap;

    if (!scene())
        LogErrorAndExit("Light: can't request shadow map for light that is not part of a scene, exiting\n");

    ASSERT(m_shadowMapSize.width() > 0 && m_shadowMapSize.height() > 0);
    Texture::TextureDescription textureDesc { .type = Texture::Type::Texture2D,
                                              .arrayCount = 1,

                                              .extent = Extent3D(m_shadowMapSize),
                                              .format = Texture::Format::Depth32F,

                                              .minFilter = Texture::MinFilter::Linear,
                                              .magFilter = Texture::MagFilter::Linear,

                                              .wrapMode = Texture::WrapModes::clampAllToEdge(),

                                              .mipmap = Texture::Mipmap::None,
                                              .multisampling = Texture::Multisampling::None };
    m_shadowMap = Backend::get().createTexture(textureDesc);

    std::string baseName;
    switch (type()) {
    case Type::DirectionalLight:
        baseName = "DirectionalLight";
        break;
    case Type::SpotLight:
        baseName = "SpotLight";
        break;
    default:
        ASSERT_NOT_REACHED();
        break;
    }
    m_shadowMap->setName(baseName + "ShadowMap");

    return *m_shadowMap;
}

RenderTarget& Light::shadowMapRenderTarget()
{
    SCOPED_PROFILE_ZONE();

    if (m_shadowMapRenderTarget)
        return *m_shadowMapRenderTarget;

    if (!scene())
        LogErrorAndExit("Light: can't request shadow map render target for light that is not part of a scene, exiting\n");

    m_shadowMapRenderTarget = Backend::get().createRenderTarget({ { RenderTarget::AttachmentType::Depth, &shadowMap() } });

    return *m_shadowMapRenderTarget;
}

RenderState& Light::getOrCreateCachedShadowMapRenderState(const std::string& cacheIdentifier, std::function<RenderState&()> creationCallback)
{
    SCOPED_PROFILE_ZONE();

    if (!scene())
        LogErrorAndExit("Light: can't get or create shadow map render state for light that is not part of a scene, exiting\n");

    auto entry = m_cachedRenderStates.find(cacheIdentifier);
    if (entry != m_cachedRenderStates.end())
        return *entry->second;

    RenderState& newRenderState = creationCallback();
    m_cachedRenderStates[cacheIdentifier] = &newRenderState;

    return newRenderState;
}

void Light::invalidateRenderStateCache()
{
    m_cachedRenderStates.clear();
}
