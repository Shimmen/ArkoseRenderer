#pragma once

#include "backend/base/BindingSet.h"
#include <vector>

struct StateBindings {
    StateBindings() = default;

    void at(uint32_t index, BindingSet&);

    void disableAutoBinding() { m_autoBinding = false; }
    bool shouldAutoBind() const { return m_autoBinding; }

    const std::vector<BindingSet*>& orderedBindingSets() const { return m_orderedBindingSets; }

    template<typename Callback>
    void forEachBindingSet(Callback callback) const
    {
        for (uint32_t index = 0; index < m_orderedBindingSets.size(); ++index) {
            BindingSet* bindingSet = m_orderedBindingSets[index];
            if (bindingSet == nullptr && shouldAutoBind())
                continue; //ARKOSE_LOG(Fatal, "Non-contiguous bindings are not supported right now! (This can probably be changed later if we want to)");
            else
                callback(index, *bindingSet);
        }
    }

    template<typename Callback>
    void forEachBinding(Callback callback) const
    {
        for (const BindingSet* set : m_orderedBindingSets) {
            if (set == nullptr && shouldAutoBind())
                continue;
            for (const ShaderBinding& bindingInfo : set->shaderBindings())
                callback(bindingInfo);
        }
    }

private:
    bool m_autoBinding { true };
    std::vector<BindingSet*> m_orderedBindingSets {};
};
