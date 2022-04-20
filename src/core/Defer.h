#pragma once

// Scoped exit, i.e. kind of like defer but instead of function scopes it works for all scopes

template<typename Func>
class AtScopeExit {
public:
    explicit AtScopeExit(Func&& func)
        : m_function(std::move(func))
    {
    }

    ~AtScopeExit()
    {
        m_function();
    }

private:
    Func m_function;
};
