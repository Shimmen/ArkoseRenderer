#include "CubeLUT.h"

#include "core/Assert.h"
#include "utility/FileIO.h"
#include "utility/ParseContext.h"

std::unique_ptr<CubeLUT> CubeLUT::load(std::string_view path)
{
    SCOPED_PROFILE_ZONE();

    ParseContext parseContext { "CUBE", std::string(path) };
    if (!parseContext.isValid()) {
        ARKOSE_LOG(Error, "CubeLUT: failed to read .cube file '{}'", path);
        return nullptr;
    }

    size_t numTableEntries = 0;

    // CubeLUT specification version 1.0:
    // https://kono.phpage.fr/images/a/a1/Adobe-cube-lut-specification-1.0.pdf
    // Comments in quotes below are taken from the specification.

    std::string title { "" }; // "If TITLE is omitted from the file, the title is undefined"
    vec3 domainMin { 0.0, 0.0, 0.0 }; // "If DOMAIN_MIN is omitted from the file, the lower bounds shall be 0 0 0"
    vec3 domainMax { 1.0, 1.0, 1.0 }; // "If DOMAIN_MAX is omitted from the file, the upper bounds shall be 1 1 1"
    size_t tableSize = 0;
    std::vector<vec4> tableData;

    while (!parseContext.isEndOfFile()) {

        if (parseContext.peekNextCharacter() == '#') {
            parseContext.nextLine(); // discard line
            continue;
        }

        // NOTE: According to spec there should be no newlines here but I found some such examples of it so let's be lenient.
        parseContext.consumeWhitespace();

        std::optional<std::string> maybeSymbol = parseContext.consumeStandardSymbol();
        std::string const& symbol = maybeSymbol.value_or("");

        if (symbol == "") {
            if (numTableEntries > 0) {

                // Read a table entry
                float red = parseContext.nextAsFloat("table value red");
                parseContext.consumeWhitespace();
                float green = parseContext.nextAsFloat("table value green");
                parseContext.consumeWhitespace();
                float blue = parseContext.nextAsFloat("table value blue");
                parseContext.consumeNewline(1, '\n');

                tableData.emplace_back(red, green, blue, 1.0f);

            } else {
                // TODO: Handle error properly..
                ARKOSE_ERROR("CubeLUT: parsing error, no symbol found but also not able to read table data yet");
            }
        } else if (symbol == "TITLE") {
            parseContext.consumeWhitespace();
            title = parseContext.consumeString('"').value_or("");
        } else if (symbol == "DOMAIN_MIN") {
            parseContext.consumeWhitespace();
            domainMin.x = parseContext.nextAsFloat("DOMAIN_MIN rl");
            parseContext.consumeWhitespace();
            domainMin.y = parseContext.nextAsFloat("DOMAIN_MIN gl");
            parseContext.consumeWhitespace();
            domainMin.z = parseContext.nextAsFloat("DOMAIN_MIN bl");
        } else if (symbol == "DOMAIN_MAX") {
            parseContext.consumeWhitespace();
            domainMax.x = parseContext.nextAsFloat("DOMAIN_MAX rh");
            parseContext.consumeWhitespace();
            domainMax.y = parseContext.nextAsFloat("DOMAIN_MAX gh");
            parseContext.consumeWhitespace();
            domainMax.z = parseContext.nextAsFloat("DOMAIN_MAX bh");
        } else if (symbol == "LUT_1D_SIZE") {
            parseContext.consumeWhitespace();
            tableSize = parseContext.nextAsInt("LUT_1D_SIZE N");
            numTableEntries = tableSize;
            tableData.reserve(numTableEntries);
        } else if (symbol == "LUT_3D_SIZE") {
            parseContext.consumeWhitespace();
            tableSize = parseContext.nextAsInt("LUT_3D_SIZE N");
            numTableEntries = tableSize * tableSize * tableSize;
            tableData.reserve(numTableEntries);
        } else {
            ARKOSE_ERROR("CubeLUT: parsing error, symbol '{}' not known", symbol);
        }

        parseContext.consumeNewline(-1, '\n');
    }

    bool is1dLut = tableData.size() == tableSize;
    bool is3dLut = tableData.size() == (tableSize * tableSize * tableSize);
    ARKOSE_ASSERTM(is1dLut || is3dLut, "At least one of these must be true", 1);

    return std::make_unique<CubeLUT>(std::move(tableData), tableSize, is3dLut, domainMin, domainMax);
}

CubeLUT::CubeLUT(std::vector<vec4>&& table, size_t tableSize, bool is3dLut, vec3 domainMin, vec3 domainMax)
    : m_table(std::move(table))
    , m_tableSize(narrow_cast<u32>(tableSize))
    , m_is3dLut(is3dLut)
    , m_domainMin(domainMin)
    , m_domainMax(domainMax)
{
}

CubeLUT::CubeLUT()
{
    // Make the most simple identity 2D LUT
    //m_tableSize = 2;
    //m_table.emplace_back(0.0f, 0.0f, 0.0f, 1.0f);
    //m_table.emplace_back(1.0f, 1.0f, 1.0f, 1.0f);

    // Make the most simple identity 3D LUT
    m_tableSize = 2;
    m_is3dLut = true;
    m_table.emplace_back(0.0f, 0.0f, 0.0f, 1.0f);
    m_table.emplace_back(1.0f, 0.0f, 0.0f, 1.0f);
    m_table.emplace_back(0.0f, 1.0f, 0.0f, 1.0f);
    m_table.emplace_back(1.0f, 1.0f, 0.0f, 1.0f);
    m_table.emplace_back(0.0f, 0.0f, 1.0f, 1.0f);
    m_table.emplace_back(1.0f, 0.0f, 1.0f, 1.0f);
    m_table.emplace_back(0.0f, 1.0f, 1.0f, 1.0f);
    m_table.emplace_back(1.0f, 1.0f, 1.0f, 1.0f);
}

vec3 CubeLUT::fetch1d(i32 coord) const
{
    ARKOSE_ASSERT(is1d());

    if (coord < 0) {
        ARKOSE_LOG(Error, "CubeLUT: trying to fetch 1D with coord < 0, clamping");
        coord = 0;
    }

    if (coord >= tableSize()) {
        ARKOSE_LOG(Error, "CubeLUT: trying to fetch 1D with coord >= table size, clamping");
        coord = tableSize() - 1;
    }

    return m_table[coord].xyz();
}

vec3 CubeLUT::fetch3d(ivec3 coord) const
{
    ARKOSE_ASSERT(is3d());

    if (any(lessThan(coord, ivec3(0)))) {
        ARKOSE_LOG(Error, "CubeLUT: trying to fetch with coord < 0, clamping");
        coord = ark::max(coord, ivec3(0));
    }

    if (any(greaterThanEqual(coord, ivec3(tableSize())))) {
        ARKOSE_LOG(Error, "CubeLUT: trying to fetch with coord >= table size, clamping");
        coord = ark::max(coord, ivec3(tableSize() - 1));
    }

    i32 linearCoord = coord.x + (coord.y * tableSize()) + (coord.z * tableSize() * tableSize());
    return m_table[linearCoord].xyz();
}

vec3 CubeLUT::sample(vec3 input) const
{
    if (any(lessThan(input, domainMin()))) {
        ARKOSE_LOG(Error, "CubeLUT: trying to sample with input less than domain minimum, clamping");
        input = ark::max(input, domainMin());
    }

    if (any(greaterThan(input, domainMax()))) {
        ARKOSE_LOG(Error, "CubeLUT: trying to sample with input greater than domain maximum, clamping");
        input = ark::min(input, domainMax());
    }

    vec3 normalizedSampleCoords = (input - domainMin()) / (domainMax() - domainMin());
    vec3 sampleCoords = normalizedSampleCoords * vec3(static_cast<float>(m_tableSize) * 0.99f);

    if (is1d()) {

        float r = ark::lerp(fetch1d(floor(sampleCoords.x)).x, fetch1d(ceil(sampleCoords.x)).x, ark::fract(sampleCoords.x));
        float g = ark::lerp(fetch1d(floor(sampleCoords.y)).y, fetch1d(ceil(sampleCoords.y)).y, ark::fract(sampleCoords.y));
        float b = ark::lerp(fetch1d(floor(sampleCoords.z)).z, fetch1d(ceil(sampleCoords.z)).z, ark::fract(sampleCoords.z));

        return vec3(r, g, b);

    } else {

        // TODO: Implement trilinear interpolation! Or maybe just don't and let the GPU handle it..

        ivec3 topLeftCoords = ivec3(static_cast<i32>(floor(sampleCoords.x)),
                                    static_cast<i32>(floor(sampleCoords.y)),
                                    static_cast<i32>(floor(sampleCoords.z)));

        vec3 topLeftRgb = fetch3d(topLeftCoords);

        return topLeftRgb;

    }
}

std::span<const float> CubeLUT::dataForGpuUpload() const
{
    if (is1d()) {
        ARKOSE_LOG(Fatal, "CubeLUT: only 3D LUTs are currently supported for GPU upload");
    }

    if (!all(domainMin() == vec3(0.0f))) {
        ARKOSE_LOG(Fatal, "CubeLUT: only LUTs with domain min of (0, 0, 0) are supported for GPU upload");
    }

    if (!all(domainMax() == vec3(1.0f))) {
        ARKOSE_LOG(Fatal, "CubeLUT: only LUTs with domain max of (1, 1, 1) are supported for GPU upload");
    }

    float const* firstFloat = &m_table[0].x;
    size_t numFloats = m_table.size() * 4;

    return std::span<const float>(firstFloat, numFloats);
}
