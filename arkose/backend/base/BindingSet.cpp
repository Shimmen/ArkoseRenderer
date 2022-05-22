#include "BindingSet.h"

#include "Buffer.h"
#include "Texture.h"
#include "core/Logging.h"
#include <algorithm>


static bool isBufferStorageCapable(Buffer& buffer)
{
    switch (buffer.usage()) {
    case Buffer::Usage::Vertex: // includes storage buffer support
    case Buffer::Usage::Index: // includes storage buffer support
    case Buffer::Usage::StorageBuffer:
    case Buffer::Usage::IndirectBuffer:
        return true;
    }

    return false;
}

static bool isTextureStorageCapable(Texture& texture)
{
    if (texture.hasSrgbFormat() || texture.hasDepthFormat()) {
        return false;
    }

    return true;
}

ShaderBinding::ShaderBinding(ShaderBindingType type, ShaderStage shaderStage, uint32_t index)
    : m_bindingIndex(index)
    , m_type(type)
    , m_shaderStage(shaderStage)
{
}

ShaderBinding ShaderBinding::constantBuffer(Buffer& buffer, ShaderStage shaderStage)
{
    ShaderBinding binding { ShaderBindingType::ConstantBuffer, shaderStage };

    ARKOSE_ASSERT(buffer.usage() == Buffer::Usage::ConstantBuffer);
    binding.m_buffers.push_back(&buffer);

    return binding;
}

ShaderBinding ShaderBinding::storageBuffer(Buffer& buffer, ShaderStage shaderStage)
{
    ShaderBinding binding { ShaderBindingType::StorageBuffer, shaderStage };

    ARKOSE_ASSERT(isBufferStorageCapable(buffer));
    binding.m_buffers.push_back(&buffer);

    return binding;
}

ShaderBinding ShaderBinding::storageBufferBindlessArray(const std::vector<Buffer*>& buffers, ShaderStage shaderStage)
{
    ShaderBinding binding { ShaderBindingType::StorageBuffer, shaderStage };
    binding.m_arrayCount = static_cast<uint32_t>(buffers.size());

    binding.m_buffers.reserve(binding.m_arrayCount);
    for (Buffer* buffer : buffers) {
        ARKOSE_ASSERT(buffer);
        ARKOSE_ASSERT(isBufferStorageCapable(*buffer));
        binding.m_buffers.push_back(buffer);
    }

    return binding;
}

ShaderBinding ShaderBinding::sampledTexture(Texture& texture, ShaderStage shaderStage)
{
    ShaderBinding binding { ShaderBindingType::SampledTexture, shaderStage };

    binding.m_sampledTextures.push_back(&texture);

    return binding;
}

ShaderBinding ShaderBinding::sampledTextureBindlessArray(const std::vector<Texture*>& textures, ShaderStage shaderStage)
{
    ShaderBinding binding { ShaderBindingType::SampledTexture, shaderStage };
    binding.m_arrayCount = static_cast<uint32_t>(textures.size());

    binding.m_sampledTextures.reserve(textures.size());
    for (Texture* texture : textures) {
        ARKOSE_ASSERT(texture);
        binding.m_sampledTextures.push_back(texture);
    }

    return binding;
}

ShaderBinding ShaderBinding::sampledTextureBindlessArray(uint32_t count, const std::vector<Texture*>& textures, ShaderStage shaderStage)
{
    ShaderBinding binding { ShaderBindingType::SampledTexture, shaderStage };

    if (count < textures.size()) {
        ARKOSE_LOG(Fatal, "ShaderBinding error: too many textures in list ({}) compared to specified count {}", textures.size(), count);
    }

    binding.m_arrayCount = count;

    binding.m_sampledTextures.reserve(textures.size());
    for (Texture* texture : textures) {
        ARKOSE_ASSERT(texture);
        binding.m_sampledTextures.push_back(texture);
    }

    return binding;
}

ShaderBinding ShaderBinding::storageTexture(Texture& texture, ShaderStage shaderStage)
{
    return ShaderBinding::storageTextureAtMip(texture, 0, shaderStage);
}

ShaderBinding ShaderBinding::storageTextureAtMip(Texture& texture, uint32_t mipLevel, ShaderStage shaderStage)
{
    ShaderBinding binding { ShaderBindingType::StorageTexture, shaderStage };

    ARKOSE_ASSERT(isTextureStorageCapable(texture));
    binding.m_storageTextures.push_back(TextureMipView(texture, mipLevel));

    return binding;
}

ShaderBinding ShaderBinding::topLevelAccelerationStructure(TopLevelAS& tlas, ShaderStage shaderStage)
{
    ShaderBinding binding { ShaderBindingType::RTAccelerationStructure, shaderStage };

    binding.m_topLevelAS = &tlas;

    return binding;
}

ShaderBinding::ShaderBinding(uint32_t index, ShaderStage shaderStage, Buffer* buffer)
    : m_bindingIndex(index)
    , m_arrayCount(1)
    , m_shaderStage(shaderStage)
    , m_topLevelAS(nullptr)
    , m_buffers({ buffer })
    , m_sampledTextures()
    , m_storageTextures()
{
    if (!buffer) {
        ARKOSE_LOG(Fatal, "ShaderBinding error: null buffer");
    }

    switch (buffer->usage()) {
    case Buffer::Usage::ConstantBuffer:
        m_type = ShaderBindingType::ConstantBuffer;
        break;
    case Buffer::Usage::Vertex: // includes storage buffer support
    case Buffer::Usage::Index: // includes storage buffer support
    case Buffer::Usage::StorageBuffer:
    case Buffer::Usage::IndirectBuffer:
        m_type = ShaderBindingType::StorageBuffer;
        break;
    default:
        ARKOSE_LOG(Fatal, "ShaderBinding error: invalid buffer for shader binding (not index or uniform buffer)");
    }
}

ShaderBinding::ShaderBinding(uint32_t index, ShaderStage shaderStage, Texture* texture, ShaderBindingType type)
    : m_bindingIndex(index)
    , m_arrayCount(1)
    , m_shaderStage(shaderStage)
    , m_type(type)
    , m_topLevelAS(nullptr)
    , m_buffers()
    , m_sampledTextures()
    , m_storageTextures()
{
    if (!texture) {
        ARKOSE_LOG(Fatal, "ShaderBinding error: null texture");
    }

    if (type == ShaderBindingType::StorageTexture) {
        if (texture->hasSrgbFormat() || texture->hasDepthFormat()) {
            ARKOSE_LOG(Fatal, "ShaderBinding error: can't use texture with sRGB or depth format as storage image");
        }
        m_storageTextures.push_back(TextureMipView(*texture, 0));
    }

    if (type == ShaderBindingType::SampledTexture) {
        m_sampledTextures.push_back(texture);
    }

}

ShaderBinding::ShaderBinding(uint32_t index, ShaderStage shaderStage, TextureMipView textureMip, ShaderBindingType type)
    : m_bindingIndex(index)
    , m_arrayCount(1)
    , m_shaderStage(shaderStage)
    , m_type(type)
    , m_topLevelAS(nullptr)
    , m_buffers()
    , m_sampledTextures()
    , m_storageTextures()
{
    if (type != ShaderBindingType::StorageTexture) {
        ARKOSE_LOG(Fatal, "ShaderBinding error: trying to pass a specific texture mip but not using storage image binding type");
    }

    m_storageTextures.emplace_back(std::move(textureMip));
}

ShaderBinding::ShaderBinding(uint32_t index, ShaderStage shaderStage, TopLevelAS* tlas)
    : m_bindingIndex(index)
    , m_arrayCount(1)
    , m_shaderStage(shaderStage)
    , m_type(ShaderBindingType::RTAccelerationStructure)
    , m_topLevelAS(tlas)
    , m_buffers()
    , m_sampledTextures()
    , m_storageTextures()
{
    if (!tlas) {
        ARKOSE_LOG(Fatal, "ShaderBinding error: null acceleration structure");
    }
}

ShaderBinding::ShaderBinding(uint32_t index, ShaderStage shaderStage, uint32_t count, const std::vector<Texture*>& textures)
    : m_bindingIndex(index)
    , m_arrayCount(count)
    , m_shaderStage(shaderStage)
    , m_type(ShaderBindingType::SampledTexture)
    , m_topLevelAS(nullptr)
    , m_buffers()
    , m_sampledTextures(textures)
    , m_storageTextures()
{
    if (count < textures.size()) {
        ARKOSE_LOG(Fatal, "ShaderBinding error: too many textures in list");
    }

    for (auto texture : textures) {
        if (!texture) {
            ARKOSE_LOG(Fatal, "ShaderBinding error: null texture in list");
        }
    }
}

ShaderBinding::ShaderBinding(uint32_t index, ShaderStage shaderStage, const std::vector<Texture*>& textures)
    : m_bindingIndex(index)
    , m_arrayCount((uint32_t)textures.size())
    , m_shaderStage(shaderStage)
    , m_type(ShaderBindingType::SampledTexture)
    , m_topLevelAS(nullptr)
    , m_buffers()
    , m_sampledTextures(textures)
    , m_storageTextures()
{
    if (m_arrayCount < 1) {
        //ARKOSE_LOG(Fatal, "ShaderBinding error: too few textures in list");
    }

    for (auto texture : textures) {
        if (!texture) {
            ARKOSE_LOG(Fatal, "ShaderBinding error: null texture in list");
        }
    }
}

ShaderBinding::ShaderBinding(uint32_t index, ShaderStage shaderStage, const std::vector<Buffer*>& buffers)
    : m_bindingIndex(index)
    , m_arrayCount((uint32_t)buffers.size())
    , m_shaderStage(shaderStage)
    , m_type(ShaderBindingType::StorageBuffer)
    , m_topLevelAS(nullptr)
    , m_buffers(buffers)
    , m_sampledTextures()
    , m_storageTextures()
{
    if (m_arrayCount < 1) {
        //ARKOSE_LOG(Fatal, "ShaderBinding error: too few buffers in list");
    }

    for (auto buffer : buffers) {
        if (!buffer) {
            ARKOSE_LOG(Fatal, "ShaderBinding error: null buffer in list");
        }
        if (buffer->usage() != Buffer::Usage::StorageBuffer && buffer->usage() != Buffer::Usage::IndirectBuffer) {
            ARKOSE_LOG(Fatal, "ShaderBinding error: buffer in list is not a storage buffer");
        }
    }
}


BindingSet::BindingSet(Backend& backend, std::vector<ShaderBinding> shaderBindings)
    : Resource(backend)
    , m_shaderBindings(std::move(shaderBindings))
{
    ARKOSE_ASSERT(m_shaderBindings.size() >= 1);
    bool assignImplicitIndices = m_shaderBindings.front().bindingIndex() == ShaderBinding::ImplicitIndex;

    if (assignImplicitIndices) {

        for (uint32_t idx = 0; idx < m_shaderBindings.size(); ++idx) {
            ShaderBinding& binding = m_shaderBindings[idx];

            // Ensure that if any have implicit index, all of them need to have implicit index
            ARKOSE_ASSERT(binding.bindingIndex() == ShaderBinding::ImplicitIndex);

            binding.updateBindingIndex({}, idx);
        }

    } else {

        // In case of explicit indices, sort them and ensure there are no duplicates

        std::sort(m_shaderBindings.begin(), m_shaderBindings.end(), [](const ShaderBinding& left, const ShaderBinding& right) {
            ARKOSE_ASSERT(left.bindingIndex() != ShaderBinding::ImplicitIndex);
            ARKOSE_ASSERT(right.bindingIndex() != ShaderBinding::ImplicitIndex);
            return left.bindingIndex() < right.bindingIndex();
        });

        for (size_t idx = 0; idx < m_shaderBindings.size() - 1; ++idx) {
            if (m_shaderBindings[idx].bindingIndex() == m_shaderBindings[idx + 1].bindingIndex()) {
                ARKOSE_LOG(Fatal, "BindingSet error: duplicate bindings");
            }
        }
    }
}
