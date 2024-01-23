#pragma once

#include "rendering/backend/base/BindingSet.h"
#include <vector>

struct StateBindings {
    StateBindings() = default;

    void at(u32 index, BindingSet&);

    const std::vector<BindingSet*>& orderedBindingSets() const { return m_orderedBindingSets; }

    template<typename Callback>
    void forEachBindingSet(Callback callback) const
    {
        for (u32 index = 0; index < m_orderedBindingSets.size(); ++index) {
            if (BindingSet* bindingSet = m_orderedBindingSets[index]) {
                callback(index, *bindingSet);
            }
        }
    }

    template<typename Callback>
    void forEachBinding(Callback callback) const
    {
        for (BindingSet const* set : m_orderedBindingSets) {
            if (set != nullptr) {
                for (ShaderBinding const& bindingInfo : set->shaderBindings()) {
                    callback(bindingInfo);
                }
            }
        }
    }

private:
    std::vector<BindingSet*> m_orderedBindingSets {};
};
