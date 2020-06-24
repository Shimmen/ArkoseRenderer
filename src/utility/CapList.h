#pragma once

#include "Logging.h"
#include <vector>

/// Defines a list with a fixed capacity. It's pretty much like std::array but it doesn't have the capacity as a template argument.
/// A use case for this is when you want vector like semantics but need the data to never move, so addresses can be kept. This is
/// important for the Registry class, where you add resources as you want, the manager owns the resources, and it hands out
/// addresses to the callers.
template<typename T>
class CapList {
public:
    explicit CapList(size_t cap)
        : m_internal()
    {
        m_internal.reserve(cap);
    }

    void push_back(T val)
    {
        if (m_internal.size() >= m_internal.capacity()) {
            LogErrorAndExit("CapList: reached max capacity %i.\n", m_internal.capacity());
        }
        m_internal.push_back(val);
    }

    [[nodiscard]] size_t size() const
    {
        return m_internal.size();
    }

    T& back()
    {
        return m_internal.back();
    }
    const T& back() const
    {
        return m_internal.back();
    }

    const std::vector<T>& vector() const
    {
        return m_internal;
    }

private:
    std::vector<T> m_internal;
};
