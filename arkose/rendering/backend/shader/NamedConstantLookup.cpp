#include "NamedConstantLookup.h"

NamedConstantLookup::NamedConstantLookup(std::vector<NamedConstant> const& mergedNamedConstants)
{
    for (NamedConstant const& namedConstant : mergedNamedConstants) { 
        m_lookupMap[namedConstant.name] = namedConstant;
    }
}

NamedConstant const* NamedConstantLookup::lookupConstant(std::string const& constantName) const
{
    auto entry = m_lookupMap.find(constantName);
    if (entry != m_lookupMap.end()) { 
        return &entry->second;
    } else {
        return nullptr;
    }
}

std::optional<u32> NamedConstantLookup::lookupConstantOffset(std::string const& constantName, size_t expectedSize) const
{
    NamedConstant const* constant = lookupConstant(constantName);
    if (constant == nullptr) {
        return std::nullopt;
    }

    if (!validateConstant(*constant, expectedSize)) { 
        return std::nullopt;
    }

    return constant->offset;
}

bool NamedConstantLookup::validateConstant(NamedConstant const& constant, size_t expectedSize) const
{
    if (constant.size != expectedSize) {
        ARKOSE_LOG(Error, "NamedConstantLookup: constant '{}' has mismatching sizes (actual: {}, expected: {}).", constant.name, constant.size, expectedSize);
        return false;
    }

    return true;
}
