#pragma once

#include "backend/Resource.h"
#include "backend/base/BindingSet.h"

// TODO: Avoid importing frontend stuff from backend
#include "rendering/Shader.h"

class ComputeState : public Resource {
public:
    ComputeState() = default;
    ComputeState(Backend&, Shader, std::vector<BindingSet*>);

    const Shader& shader() const { return m_shader; }
    [[nodiscard]] const std::vector<BindingSet*>& bindingSets() const { return m_bindingSets; }

private:
    Shader m_shader;
    std::vector<BindingSet*> m_bindingSets;
};
