#include "ComputeState.h"

ComputeState::ComputeState(Backend& backend, Shader shader, std::vector<BindingSet*> bindingSets)
    : Resource(backend)
    , m_shader(shader)
    , m_bindingSets(bindingSets)
{
}
