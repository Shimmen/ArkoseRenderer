#include "BindingSet.h"

#include "Buffer.h"
#include "Texture.h"
#include "core/Logging.h"
#include <algorithm>


// TODO: Move this to the Texture class, similarly to how Buffer does it
static bool isTextureStorageCapable(Texture& texture)
{
    if (texture.hasSrgbFormat() || texture.hasDepthFormat()) {
        return false;
    }

    return true;
}

ShaderBinding::ShaderBinding(ShaderBindingType type, ShaderStage shaderStage)
    : m_type(type)
    , m_shaderStage(shaderStage)
{
}

ShaderBinding ShaderBinding::constantBuffer(Buffer const& buffer, ShaderStage shaderStage)
{
    ShaderBinding binding { ShaderBindingType::ConstantBuffer, shaderStage };

    ARKOSE_ASSERT(buffer.usage() == Buffer::Usage::ConstantBuffer);
    binding.m_buffers.push_back(const_cast<Buffer*>(&buffer));

    return binding;
}

ShaderBinding ShaderBinding::storageBuffer(Buffer& buffer, ShaderStage shaderStage)
{
    ShaderBinding binding { ShaderBindingType::StorageBuffer, shaderStage };

    ARKOSE_ASSERT(buffer.storageCapable());
    binding.m_buffers.push_back(&buffer);

    return binding;
}

ShaderBinding ShaderBinding::storageBufferReadonly(Buffer const& buffer, ShaderStage shaderStage)
{
    ShaderBinding binding { ShaderBindingType::StorageBuffer, shaderStage };

    ARKOSE_ASSERT(buffer.storageCapable());
    binding.m_buffers.push_back(const_cast<Buffer*>(&buffer));

    return binding;
}

ShaderBinding ShaderBinding::storageBufferBindlessArray(const std::vector<Buffer*>& buffers, ShaderStage shaderStage)
{
    ShaderBinding binding { ShaderBindingType::StorageBuffer, shaderStage };
    binding.m_arrayCount = static_cast<uint32_t>(buffers.size());

    binding.m_buffers.reserve(binding.m_arrayCount);
    for (Buffer* buffer : buffers) {
        ARKOSE_ASSERT(buffer);
        ARKOSE_ASSERT(buffer->storageCapable());
        binding.m_buffers.push_back(buffer);
    }

    return binding;
}

ShaderBinding ShaderBinding::sampledTexture(Texture const& texture, ShaderStage shaderStage)
{
    ShaderBinding binding { ShaderBindingType::SampledTexture, shaderStage };

    binding.m_sampledTextures.push_back(&texture);

    return binding;
}

ShaderBinding ShaderBinding::sampledTextureBindlessArray(const std::vector<Texture const*>& textures, ShaderStage shaderStage)
{
    ShaderBinding binding { ShaderBindingType::SampledTexture, shaderStage };
    binding.m_arrayCount = static_cast<uint32_t>(textures.size());

    binding.m_sampledTextures.reserve(textures.size());
    for (Texture const* texture : textures) {
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

BindingSet::BindingSet(Backend& backend, std::vector<ShaderBinding> shaderBindings)
    : Resource(backend)
    , m_shaderBindings(std::move(shaderBindings))
{
    ARKOSE_ASSERT(m_shaderBindings.size() >= 1);

    bool assignImplicitIndices = m_shaderBindings.front().bindingIndex() == ShaderBinding::ImplicitIndex;
    ARKOSE_ASSERT(assignImplicitIndices); // There are no longer any APIs that allow you to set indices explicitly!

    for (uint32_t idx = 0; idx < m_shaderBindings.size(); ++idx) {
        ShaderBinding& binding = m_shaderBindings[idx];

        // Ensure that if any have implicit index, all of them need to have implicit index
        ARKOSE_ASSERT(binding.bindingIndex() == ShaderBinding::ImplicitIndex);

        binding.updateBindingIndex({}, idx);
    }
}
