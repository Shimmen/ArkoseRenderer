#pragma once

#include "core/Types.h"
#include <string>

class MorphTarget {
public:
    explicit MorphTarget(std::string_view name);

    std::string const& name() { return m_name; }

    float weight { 0.0f };

private:
    std::string m_name;
};
