#include "ComputeState.h"

ComputeState::ComputeState(Backend& backend, Shader shader, StateBindings const& stateBindings)
    : Resource(backend)
    , m_shader(shader)
    , m_stateBindings(stateBindings)
{
}
