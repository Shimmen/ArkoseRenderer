#pragma once

#include <string>

struct NodeDependency {
public:
    NodeDependency(std::string neededIn, std::string comesFrom)
        : m_neededIn(neededIn)
        , m_comesFrom(comesFrom)
    {
    }

    bool operator==(const NodeDependency& other) const
    {
        return (m_neededIn == other.m_neededIn) && (m_comesFrom == other.m_comesFrom);
    }

    [[nodiscard]] const std::string& neededIn() const { return m_neededIn; }
    [[nodiscard]] const std::string& comesFrom() const { return m_comesFrom; }

private:
    std::string m_neededIn;
    std::string m_comesFrom;
};

namespace std {

template<>
struct hash<NodeDependency> {
    std::size_t operator()(const NodeDependency& dep) const
    {
        return std::hash<std::string>()(dep.neededIn()) ^ (std::hash<std::string>()(dep.comesFrom()) << 1);
    }
};
}
