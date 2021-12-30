#include "StateBindings.h"

#include "utility/util.h"

void StateBindings::at(uint32_t index, BindingSet& bindingSet)
{
    if (index >= (int64_t)m_orderedBindingSets.size()) {
        m_orderedBindingSets.resize(size_t(index) + 1);
    }

    ASSERT(m_orderedBindingSets[index] == nullptr);
    m_orderedBindingSets[index] = &bindingSet;
}
