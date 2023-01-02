#ifndef NAMED_UNIFORMS_GLSL
#define NAMED_UNIFORMS_GLSL

// This construct allows us to get the names of the individual members of a push constant rage in Vulkan
// It also nicely translates to OpenGL or HLSL if that's something we want at some later stage.

#define NAMED_UNIFORMS(scopeName, uniforms)        \
    struct PushConstantStruct {                    \
        uniforms                                   \
    };                                             \
    layout(push_constant) uniform PushConstants {  \
        PushConstantStruct scopeName;              \
    };

#define NAMED_UNIFORMS_STRUCT(PushConstantStructName, scopeName)  \
    layout(push_constant) uniform PushConstantsBlockName {        \
        PushConstantStructName scopeName;                         \
    };

#endif // NAMED_UNIFORMS_GLSL
