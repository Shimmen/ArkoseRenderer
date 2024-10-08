#pragma once

#include "rendering/backend/Resource.h"
#include "rendering/backend/base/BindingSet.h"
#include "rendering/backend/util/StateBindings.h"
#include "rendering/backend/shader/NamedConstantLookup.h"
#include "rendering/backend/shader/Shader.h"

class ComputeState : public Resource {
public:
    ComputeState() = default;
    ComputeState(Backend&, Shader, StateBindings const&);

    Shader const& shader() const { return m_shader; }
    StateBindings const& stateBindings() const { return m_stateBindings; }
    NamedConstantLookup const& namedConstantLookup() const { return m_namedConstantLookup; }

protected:
    NamedConstantLookup m_namedConstantLookup;

private:
    Shader m_shader;
    StateBindings m_stateBindings;
};
