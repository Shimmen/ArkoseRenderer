#pragma once

#include "rendering/backend/Resource.h"
#include "rendering/backend/base/Texture.h"
#include "rendering/backend/shader/Shader.h"
#include <vector>

class Buffer;
class Texture;
class TopLevelAS;

enum class ShaderBindingType {
    ConstantBuffer,
    StorageBuffer,
    StorageTexture,
    SampledTexture,
    RTAccelerationStructure,
};

class ShaderBinding {
public:

    ShaderBinding(ShaderBindingType, ShaderStage);

    // New, self-explanatory API (and with implicit index)

    static ShaderBinding constantBuffer(Buffer const&, ShaderStage = ShaderStage::Any);
    static ShaderBinding storageBuffer(Buffer&, ShaderStage = ShaderStage::Any);
    static ShaderBinding storageBufferReadonly(Buffer const&, ShaderStage = ShaderStage::Any); // NOTE: The readonly property is not guaranteed by this function!
    static ShaderBinding storageBufferBindlessArray(const std::vector<Buffer*>&, ShaderStage = ShaderStage::Any);

    static ShaderBinding sampledTexture(Texture const&, ShaderStage = ShaderStage::Any);
    static ShaderBinding sampledTextureBindlessArray(const std::vector<Texture const*>&, ShaderStage = ShaderStage::Any);
    static ShaderBinding sampledTextureBindlessArray(uint32_t count, const std::vector<Texture*>&, ShaderStage = ShaderStage::Any);
    
    static ShaderBinding storageTexture(Texture&, ShaderStage = ShaderStage::Any);
    static ShaderBinding storageTextureAtMip(Texture&, uint32_t mipLevel, ShaderStage = ShaderStage::Any);

    static ShaderBinding topLevelAccelerationStructure(TopLevelAS&, ShaderStage = ShaderStage::Any);

    //

    ShaderBindingType type() const { return m_type; }
    uint32_t arrayCount() const { return m_arrayCount; }

    ShaderStage shaderStage() const { return m_shaderStage; }

    uint32_t bindingIndex() const { return m_bindingIndex; }
    void updateBindingIndex(Badge<class BindingSet>, uint32_t index) { m_bindingIndex = index; }

    const Buffer& getBuffer() const
    {
        ARKOSE_ASSERT(type() == ShaderBindingType::ConstantBuffer || type() == ShaderBindingType::StorageBuffer);
        ARKOSE_ASSERT(m_buffers.size() == 1);
        return *m_buffers.front();
    }

    const std::vector<Buffer*>& getBuffers() const
    {
        ARKOSE_ASSERT(type() == ShaderBindingType::ConstantBuffer || type() == ShaderBindingType::StorageBuffer);
        ARKOSE_ASSERT(m_buffers.size() >= 1);
        return m_buffers;
    }

    const TopLevelAS& getTopLevelAS() const
    {
        ARKOSE_ASSERT(type() == ShaderBindingType::RTAccelerationStructure);
        ARKOSE_ASSERT(m_topLevelAS != nullptr);
        return *m_topLevelAS;
    }

    const Texture* getSampledTexture() const
    {
        ARKOSE_ASSERT(type() == ShaderBindingType::SampledTexture);
        ARKOSE_ASSERT(m_sampledTextures.size() == 1);
        return m_sampledTextures.front();
    }

    const std::vector<Texture const*>& getSampledTextures() const
    {
        ARKOSE_ASSERT(type() == ShaderBindingType::SampledTexture);
        //ARKOSE_ASSERT(m_sampledTextures.size() >= 1);
        return m_sampledTextures;
    }

    const TextureMipView& getStorageTexture() const
    {
        ARKOSE_ASSERT(type() == ShaderBindingType::StorageTexture);
        ARKOSE_ASSERT(m_storageTextures.size() == 1);
        return m_storageTextures.front();
    }

    const std::vector<TextureMipView>& getStorageTextures() const
    {
        ARKOSE_ASSERT(type() == ShaderBindingType::StorageTexture);
        ARKOSE_ASSERT(m_storageTextures.size() >= 1);
        return m_storageTextures;
    }

    static constexpr uint32_t ImplicitIndex = UINT32_MAX;

private:
    uint32_t m_bindingIndex { ImplicitIndex };
    ShaderBindingType m_type;
    ShaderStage m_shaderStage;

    uint32_t m_arrayCount { 1 };

    std::vector<Buffer*> m_buffers {};
    std::vector<Texture const*> m_sampledTextures {};
    std::vector<TextureMipView> m_storageTextures {};
    TopLevelAS* m_topLevelAS { nullptr };
};

class BindingSet : public Resource {
public:

    BindingSet() = default;
    BindingSet(Backend&, std::vector<ShaderBinding>);

    struct TextureBindingUpdate {
        Texture* texture {};
        uint32_t index { 0 };
    };

    virtual void updateTextures(uint32_t index, const std::vector<TextureBindingUpdate>&) = 0;

    const std::vector<ShaderBinding>& shaderBindings() const { return m_shaderBindings; }

private:
    std::vector<ShaderBinding> m_shaderBindings {};
};
