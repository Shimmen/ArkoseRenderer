#pragma once

#include "asset/HairAsset.h"
#include "core/Types.h"
#include <ark/vector.h>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

//
// .hair spec & sample files: https://www.cemyuksel.com/research/hairmodels/
//
class HairFile {
public:
    static std::unique_ptr<HairFile> load(std::filesystem::path const& path);

    HairFile() = default;
    ~HairFile() = default;

    HairFile(HairFile const&) = delete;
    HairFile& operator=(HairFile const&) = delete;

    u32 strandCount() const { return m_strandCount; }
    u32 pointCount() const { return m_pointCount; }

    std::span<const vec3> points() const { return m_points; }
    std::span<const u16> segments() const { return m_segments; }
    std::span<const float> thickness() const { return m_thickness; }
    std::span<const float> transparency() const { return m_transparency; }
    std::span<const vec3> colors() const { return m_colors; }

    std::string const& fileInfo() const { return m_fileInfo; }

    u32 segmentsForStrand(u32 strandIdx) const;
    float thicknessForPoint(u32 pointIdx) const;
    float transparencyForPoint(u32 pointIdx) const;
    vec3 colorForPoint(u32 pointIdx) const;

    std::unique_ptr<HairAsset> createHairAsset() const;

private:
    bool readFromFile(std::filesystem::path const& path);

    std::string m_name {};

    u32 m_strandCount { 0 };
    u32 m_pointCount { 0 };

    u32 m_defaultSegments { 0 };
    float m_defaultThickness { 1.0f };
    float m_defaultTransparency { 0.0f };
    vec3 m_defaultColor { 1.0f, 1.0f, 1.0f };

    std::vector<u16> m_segments {};
    std::vector<vec3> m_points {};
    std::vector<float> m_thickness {};
    std::vector<float> m_transparency {};
    std::vector<vec3> m_colors {};

    std::string m_fileInfo {};
};
