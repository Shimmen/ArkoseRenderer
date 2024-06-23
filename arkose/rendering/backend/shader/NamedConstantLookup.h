#pragma once

#include "rendering/backend/shader/NamedConstant.h"
#include <optional>
#include <string>
#include <unordered_map>

class NamedConstantLookup {
public:
    NamedConstantLookup() = default;
    explicit NamedConstantLookup(std::vector<NamedConstant> const& mergedNamedConstants);

    NamedConstant const* lookupConstant(std::string const& constantName) const;
    std::optional<u32> lookupConstantOffset(std::string const& constantName, size_t expectedSize) const;

    bool validateConstant(NamedConstant const&, size_t expectedSize) const;

    bool empty() const { return m_lookupMap.empty(); }
    u32 totalOccupiedSize() const;

private:
    std::unordered_map<std::string, NamedConstant> m_lookupMap {};
    u32 m_totalOccupiedSize { 0 };
};
