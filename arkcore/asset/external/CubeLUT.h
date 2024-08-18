#pragma once

#include "core/Types.h"
#include <ark/vector.h>
#include <memory>
#include <string_view>
#include <vector>

class CubeLUT {
public:
    static std::unique_ptr<CubeLUT> load(std::string_view path);

    CubeLUT(std::vector<vec3>&& table, size_t tableSize, bool is3dLut, vec3 domainMin, vec3 domainMax);
    CubeLUT();

    CubeLUT(CubeLUT const&) = default;
    ~CubeLUT() = default;

    bool is1d() const { return !m_is3dLut; }
    bool is3d() const { return m_is3dLut; }

    u32 tableSize() const { return m_tableSize; }

    vec3 domainMin() const { return m_domainMin; }
    vec3 domainMax() const { return m_domainMax; }

    vec3 fetch1d(i32) const;
    vec3 fetch3d(ivec3) const;

    vec3 sample(vec3) const;

private:
    std::vector<vec3> m_table{};
    u32 m_tableSize{ 0 };
    bool m_is3dLut{ false };
    vec3 m_domainMin{ 0.0f };
    vec3 m_domainMax{ 1.0f };
};
