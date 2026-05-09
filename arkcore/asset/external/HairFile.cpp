#include "HairFile.h"

#include "core/Assert.h"
#include "core/Logging.h"
#include "utility/Profiling.h"

#include <ark/aabb.h>
#include <ark/vector.h>
#include <fstream>

namespace {

constexpr u32 HasSegmentsBit = 1u << 0;
constexpr u32 HasPointsBit = 1u << 1;
constexpr u32 HasThicknessBit = 1u << 2;
constexpr u32 HasTransparencyBit = 1u << 3;
constexpr u32 HasColorBit = 1u << 4;

}

std::unique_ptr<HairFile> HairFile::load(std::filesystem::path const& path)
{
    SCOPED_PROFILE_ZONE();

    auto hairFile = std::make_unique<HairFile>();
    if (!hairFile->readFromFile(path)) {
        return nullptr;
    }

    return hairFile;
}

bool HairFile::readFromFile(std::filesystem::path const& path)
{
    SCOPED_PROFILE_ZONE();

    // .hair files have no name, use the filename as the name
    m_name = path.filename().string();

    std::ifstream file { path, std::ios::binary };
    if (!file.is_open()) {
        ARKOSE_LOG(Error, "HairFile: failed to open '{}'", path);
        return false;
    }

    auto readArray = []<typename T>(std::ifstream& stream, std::vector<T>& dst, size_t count) -> bool {
        dst.resize(count);
        stream.read(reinterpret_cast<char*>(dst.data()), sizeof(T) * count);
        return stream.good();
    };

    // 128-byte header

    char magic[4];
    file.read(magic, 4);
    if (magic[0] != 'H' || magic[1] != 'A' || magic[2] != 'I' || magic[3] != 'R') {
        ARKOSE_LOG(Error, "HairFile: bad magic, not a valid .hair file '{}'", path);
        return false;
    }

    u32 bits = 0;
    file.read(reinterpret_cast<char*>(&m_strandCount), sizeof(u32));
    file.read(reinterpret_cast<char*>(&m_pointCount), sizeof(u32));
    file.read(reinterpret_cast<char*>(&bits), sizeof(u32));

    file.read(reinterpret_cast<char*>(&m_defaultSegments), sizeof(u32));
    file.read(reinterpret_cast<char*>(&m_defaultThickness), sizeof(f32));
    file.read(reinterpret_cast<char*>(&m_defaultTransparency), sizeof(f32));

    f32 defaultColor[3] {};
    file.read(reinterpret_cast<char*>(defaultColor), sizeof(f32) * 3);
    m_defaultColor = vec3(defaultColor[0], defaultColor[1], defaultColor[2]);

    char info[88] {};
    file.read(info, 88);
    m_fileInfo.assign(info, strnlen(info, sizeof(info)));

    if (!file.good()) {
        ARKOSE_LOG(Error, "HairFile: failed to read header from '{}'", path);
        return false;
    }

    if ((bits & HasPointsBit) == 0) {
        ARKOSE_LOG(Error, "HairFile: file '{}' is missing required points array", path);
        return false;
    }

    // Optional segments array

    if (bits & HasSegmentsBit) {
        if (!readArray(file, m_segments, m_strandCount)) {
            ARKOSE_LOG(Error, "HairFile: failed to read segments array from '{}'", path);
            return false;
        }
    }

    // Required points array

    {
        std::vector<f32> rawPoints;
        if (!readArray(file, rawPoints, static_cast<size_t>(m_pointCount) * 3)) {
            ARKOSE_LOG(Error, "HairFile: failed to read points array from '{}'", path);
            return false;
        }

        m_points.reserve(m_pointCount);
        for (u32 i = 0; i < m_pointCount; ++i) {
            m_points.emplace_back(rawPoints[3 * i + 0],
                                  rawPoints[3 * i + 1],
                                  rawPoints[3 * i + 2]);
        }
    }

    // Optional thickness array

    if (bits & HasThicknessBit) {
        if (!readArray(file, m_thickness, m_pointCount)) {
            ARKOSE_LOG(Error, "HairFile: failed to read thickness array from '{}'", path);
            return false;
        }
    }

    // Optional transparency array

    if (bits & HasTransparencyBit) {
        if (!readArray(file, m_transparency, m_pointCount)) {
            ARKOSE_LOG(Error, "HairFile: failed to read transparency array from '{}'", path);
            return false;
        }
    }

    // Optional color array

    if (bits & HasColorBit) {
        std::vector<f32> rawColors;
        if (!readArray(file, rawColors, static_cast<size_t>(m_pointCount) * 3)) {
            ARKOSE_LOG(Error, "HairFile: failed to read color array from '{}'", path);
            return false;
        }

        m_colors.reserve(m_pointCount);
        for (u32 i = 0; i < m_pointCount; ++i) {
            m_colors.emplace_back(rawColors[3 * i + 0],
                                  rawColors[3 * i + 1],
                                  rawColors[3 * i + 2]);
        }
    }

    ARKOSE_LOG(Info, "HairFile: loaded '{}' ({} strands, {} points)", path, m_strandCount, m_pointCount);

    return true;
}

u32 HairFile::segmentsForStrand(u32 strandIdx) const
{
    ARKOSE_ASSERT(strandIdx < m_strandCount);
    if (m_segments.empty()) {
        return m_defaultSegments;
    }
    return static_cast<u32>(m_segments[strandIdx]);
}

float HairFile::thicknessForPoint(u32 pointIdx) const
{
    ARKOSE_ASSERT(pointIdx < m_pointCount);
    if (m_thickness.empty()) {
        return m_defaultThickness;
    }
    return m_thickness[pointIdx];
}

float HairFile::transparencyForPoint(u32 pointIdx) const
{
    ARKOSE_ASSERT(pointIdx < m_pointCount);
    if (m_transparency.empty()) {
        return m_defaultTransparency;
    }
    return m_transparency[pointIdx];
}

vec3 HairFile::colorForPoint(u32 pointIdx) const
{
    ARKOSE_ASSERT(pointIdx < m_pointCount);
    if (m_colors.empty()) {
        return m_defaultColor;
    }
    return m_colors[pointIdx];
}

std::unique_ptr<HairAsset> HairFile::createHairAsset() const
{
    SCOPED_PROFILE_ZONE();

    auto hairAsset = std::make_unique<HairAsset>();
    hairAsset->name = m_name;
    hairAsset->strandCount = m_strandCount;
    hairAsset->pointCount = m_pointCount;
    hairAsset->defaultSegmentCount = m_defaultSegments;
    hairAsset->defaultThickness = m_defaultThickness;
    hairAsset->defaultTransparency = m_defaultTransparency;
    hairAsset->defaultColor = m_defaultColor;
    hairAsset->segments = m_segments;
    hairAsset->points = m_points;
    hairAsset->thickness = m_thickness;
    hairAsset->transparency = m_transparency;
    hairAsset->colors = m_colors;

    // The .hair spec doesn't say, but I all files I've come across are in centimeters,
    // so convert to meters here, as it's the canonical unit in Arkose.
    for (vec3& point : hairAsset->points) {
        point *= 0.01f;
    }

    // Calculate bounding box
    if (hairAsset->points.empty()) {
        hairAsset->boundingBox = ark::aabb3(vec3(0.0f), vec3(0.0f));
    } else {
        ark::aabb3 bounds {};
        for (vec3 const& p : hairAsset->points) {
            bounds.expandWithPoint(p);
        }
        hairAsset->boundingBox = bounds;
    }

    return hairAsset;
}
