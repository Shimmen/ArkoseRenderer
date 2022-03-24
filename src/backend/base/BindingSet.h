#pragma once

#include "backend/Resource.h"
#include "backend/shader/Shader.h"
#include <vector>

class Buffer;
class Texture;
class TopLevelAS;

enum class ShaderBindingType {
    UniformBuffer,
    StorageBuffer,
    StorageImage,
    TextureSampler,
    TextureSamplerArray,
    StorageBufferArray,
    RTAccelerationStructure,
};

struct ShaderBinding {

    // Single uniform or storage buffer
    ShaderBinding(uint32_t index, ShaderStage, Buffer*);

    // Single sampled texture or storage image
    ShaderBinding(uint32_t index, ShaderStage, Texture*, ShaderBindingType);

    // Single top level acceleration structures
    ShaderBinding(uint32_t index, ShaderStage, TopLevelAS*);

    // Multiple sampled textures in an array of fixed size (count)
    ShaderBinding(uint32_t index, ShaderStage, const std::vector<Texture*>&, uint32_t count);

    // Multiple sampled textures in an array of undefined size
    ShaderBinding(uint32_t index, ShaderStage, const std::vector<Texture*>&);

    // Multiple storage buffers in a dynamic array
    ShaderBinding(uint32_t index, ShaderStage, const std::vector<Buffer*>&);

    uint32_t bindingIndex;
    uint32_t count;

    ShaderStage shaderStage;

    ShaderBindingType type;
    TopLevelAS* tlas;
    std::vector<Buffer*> buffers;
    std::vector<Texture*> textures;
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
