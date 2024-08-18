#include "CubeLUT.h"

#include "core/Assert.h"
#include "utility/FileIO.h"
#include "utility/ParseContext.h"

std::unique_ptr<CubeLUT> CubeLUT::load(std::string_view path)
{
    ParseContext parseContext { "CUBE", std::string(path) };
    if (!parseContext.isValid()) {
        ARKOSE_LOG(Error, "CubeLUT: failed to read .cube file '{}'", path);
        return nullptr;
    }

    int numTableEntries = 0;

    // CubeLUT specification version 1.0:
    // https://kono.phpage.fr/images/a/a1/Adobe-cube-lut-specification-1.0.pdf
    // Comments in quotes below are taken from the specification.

    std::string title { "" }; // "If TITLE is omitted from the file, the title is undefined"
    vec3 domainMin { 0.0, 0.0, 0.0 }; // "If DOMAIN_MIN is omitted from the file, the lower bounds shall be 0 0 0"
    vec3 domainMax { 1.0, 1.0, 1.0 }; // "If DOMAIN_MAX is omitted from the file, the upper bounds shall be 1 1 1"
    size_t tableSize = 0;
    std::vector<vec3> tableData;

    while (!parseContext.isEndOfFile()) {

        if (parseContext.peekNextCharacter() == '#') {
            parseContext.nextLine(); // discard line
            continue;
        }

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

                tableData.emplace_back(red, green, blue);

            } else {
                // TODO: Handle error properly..
                //ARKOSE_ERROR("CubeLUT: parsing error, no symbol found but also not able to read table data yet");
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

        parseContext.consumeNewline(1, '\n');
    }

    bool is1dLut = tableData.size() == tableSize;
    bool is3dLut = tableData.size() == (tableSize * tableSize * tableSize);
    ARKOSE_ASSERTM(is1dLut || is3dLut, "At least one of these must be true", 1);

    return std::make_unique<CubeLUT>(std::move(tableData), tableSize, is3dLut, domainMin, domainMax);
}

CubeLUT::CubeLUT(std::vector<vec3>&& table, size_t tableSize, bool is3dLut, vec3 domainMin, vec3 domainMax)
    : m_table(std::move(table))
    , m_tableSize(tableSize)
    , m_is3dLut(is3dLut)
    , m_domainMin(domainMin)
    , m_domainMax(domainMax)
{
}

CubeLUT::CubeLUT()
{
    // Make the most simple identity 2D LUT
    m_tableSize = 2;
    m_table.emplace_back(0.0f, 0.0f, 0.0f);
    m_table.emplace_back(1.0f, 1.0f, 1.0f);
}
