#ifndef INDIRECT_DATA_GLSL
#define INDIRECT_DATA_GLSL

// See https://www.khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VkDrawIndexedIndirectCommand.html
struct IndexedDrawCmd {
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int  vertexOffset;
    uint firstInstance;
};

#endif // INDIRECT_DATA_GLSL
