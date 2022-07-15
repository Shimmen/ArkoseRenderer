#pragma once

#include "rendering/backend/Resource.h"
#include "rendering/backend/base/BindingSet.h"
#include "rendering/backend/shader/Shader.h"

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
