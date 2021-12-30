#pragma once

#include "backend/Resource.h"
#include <vector>

// TODO: Avoid importing frontend stuff from backend
#include "rendering/Shader.h" // for ShaderStage enum

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

    const std::vector<ShaderBinding>& shaderBindings() const { return m_shaderBindings; }

private:
    std::vector<ShaderBinding> m_shaderBindings {};
};
