#include "BindingSet.h"

#include "Buffer.h"
#include "Texture.h"
#include "utility/Logging.h"
#include <algorithm>


ShaderBinding::ShaderBinding(uint32_t index, ShaderStage shaderStage, Buffer* buffer)
    : bindingIndex(index)
    , count(1)
    , shaderStage(shaderStage)
    , tlas(nullptr)
    , buffers({ buffer })
    , textures()
{
    if (!buffer) {
        LogErrorAndExit("ShaderBinding error: null buffer\n");
    }

    switch (buffer->usage()) {
    case Buffer::Usage::UniformBuffer:
        type = ShaderBindingType::UniformBuffer;
        break;
    case Buffer::Usage::Vertex: // includes storage buffer support
    case Buffer::Usage::Index: // includes storage buffer support
    case Buffer::Usage::StorageBuffer:
    case Buffer::Usage::IndirectBuffer:
        type = ShaderBindingType::StorageBuffer;
        break;
    default:
        LogErrorAndExit("ShaderBinding error: invalid buffer for shader binding (not index or uniform buffer)\n");
    }
}

ShaderBinding::ShaderBinding(uint32_t index, ShaderStage shaderStage, Texture* texture, ShaderBindingType type)
    : bindingIndex(index)
    , count(1)
    , shaderStage(shaderStage)
    , type(type)
    , tlas(nullptr)
    , buffers()
    , textures({ texture })
{
    if (!texture) {
        LogErrorAndExit("ShaderBinding error: null texture\n");
    }

    if (type == ShaderBindingType::StorageImage && (texture->hasSrgbFormat() || texture->hasDepthFormat())) {
        LogErrorAndExit("ShaderBinding error: can't use texture with sRGB or depth format as storage image\n");
    }
}

ShaderBinding::ShaderBinding(uint32_t index, ShaderStage shaderStage, TopLevelAS* tlas)
    : bindingIndex(index)
    , count(1)
    , shaderStage(shaderStage)
    , type(ShaderBindingType::RTAccelerationStructure)
    , tlas(tlas)
    , buffers()
    , textures()
{
    if (!tlas) {
        LogErrorAndExit("ShaderBinding error: null acceleration structure\n");
    }
}

ShaderBinding::ShaderBinding(uint32_t index, ShaderStage shaderStage, const std::vector<Texture*>& textures, uint32_t count)
    : bindingIndex(index)
    , count(count)
    , shaderStage(shaderStage)
    , type(ShaderBindingType::TextureSamplerArray)
    , tlas(nullptr)
    , buffers()
    , textures(textures)
{
    if (count < textures.size()) {
        LogErrorAndExit("ShaderBinding error: too many textures in list\n");
    }

    for (auto texture : textures) {
        if (!texture) {
            LogErrorAndExit("ShaderBinding error: null texture in list\n");
        }
    }
}

ShaderBinding::ShaderBinding(uint32_t index, ShaderStage shaderStage, const std::vector<Texture*>& textures)
    : bindingIndex(index)
    , count((uint32_t)textures.size())
    , shaderStage(shaderStage)
    , type(ShaderBindingType::TextureSamplerArray)
    , tlas(nullptr)
    , buffers()
    , textures(textures)
{
    if (count < 1) {
        //LogErrorAndExit("ShaderBinding error: too few textures in list\n");
    }

    for (auto texture : textures) {
        if (!texture) {
            LogErrorAndExit("ShaderBinding error: null texture in list\n");
        }
    }
}

ShaderBinding::ShaderBinding(uint32_t index, ShaderStage shaderStage, const std::vector<Buffer*>& buffers)
    : bindingIndex(index)
    , count((uint32_t)buffers.size())
    , shaderStage(shaderStage)
    , type(ShaderBindingType::StorageBufferArray)
    , tlas(nullptr)
    , buffers(buffers)
    , textures()
{
    if (count < 1) {
        //LogErrorAndExit("ShaderBinding error: too few buffers in list\n");
    }

    for (auto buffer : buffers) {
        if (!buffer) {
            LogErrorAndExit("ShaderBinding error: null buffer in list\n");
        }
        if (buffer->usage() != Buffer::Usage::StorageBuffer && buffer->usage() != Buffer::Usage::IndirectBuffer) {
            LogErrorAndExit("ShaderBinding error: buffer in list is not a storage buffer\n");
        }
    }
}


BindingSet::BindingSet(Backend& backend, std::vector<ShaderBinding> shaderBindings)
    : Resource(backend)
    , m_shaderBindings(shaderBindings)
{
    std::sort(m_shaderBindings.begin(), m_shaderBindings.end(), [](const ShaderBinding& left, const ShaderBinding& right) {
        return left.bindingIndex < right.bindingIndex;
    });

    for (size_t i = 0; i < m_shaderBindings.size() - 1; ++i) {
        if (m_shaderBindings[i].bindingIndex == m_shaderBindings[i + 1].bindingIndex) {
            LogErrorAndExit("BindingSet error: duplicate bindings\n");
        }
    }
}
