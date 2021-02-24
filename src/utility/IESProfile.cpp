#include "IESProfile.h"

#include "backend/Resources.h"
#include "rendering/Registry.h"
#include "rendering/scene/Scene.h"
#include "utility/FileIO.h"
#include "utility/Image.h"
#include "utility/Logging.h"
#include <moos/vector.h>

IESProfile::IESProfile(const std::string& path)
    : m_path(path)
{
    parse(path);
}

float IESProfile::requiredSpotLightConeAngle() const
{
    NOT_YET_IMPLEMENTED();

    /*

    if (m_requiredSpotLightConeAngleCache > 0.0f)
        return m_requiredSpotLightConeAngleCache;

    // No, we need to consider the largest angles where we have a signficant enough candelas that it's worth counting..
    float maxH = 0.0f;
    float maxV = 0.0f;

    for (float angleH : m_anglesH) {
        maxH = std::max(maxH, std::abs(angleH));
    }

    for (float angleV : m_anglesV) {
        maxV = std::max(maxV, std::abs(angleV));
    }

    // Map to spherical
    NOT_YET_IMPLEMENTED();

    // Figure out angle
    NOT_YET_IMPLEMENTED();
    
    return m_requiredSpotLightConeAngleCache;

    */
}

Texture& IESProfile::createLookupTexture(Scene& scene, int size)
{
    std::vector<float> pixels {};
    pixels.reserve((size_t)size * (size_t)size);

    for (int y = 0; y < size; ++y) {
        float horizontal = y / static_cast<float>(size) * 360.0f;
        for (int x = 0; x < size; ++x) {
            float vertical = x / static_cast<float>(size) * 180.0f;

            float value = lookupValue(horizontal, vertical);
            pixels.push_back(value);
        }
    }

    Image::Info info {};
    info.width = size;
    info.height = size;
    info.pixelType = Image::PixelType::Grayscale;
    info.componentType = Image::ComponentType::Float;

    Image image { Image::DataOwner::External, info, pixels.data(), pixels.size() * sizeof(float) };
    return scene.registry().createTextureFromImage(image, false, false, Texture::WrapModes::clampAllToEdge());
}

void IESProfile::parse(const std::string& path)
{
    // We should never call parse a second time
    ASSERT(m_anglesV.size() == 0 && m_anglesH.size() == 0 && m_candelaValues.size() == 0);

    FileIO::ParseContext parseContext { "IES", path };
    if (!parseContext.isValid()) {
        LogErrorAndExit("IESProfile: could not read .ies file '%s'\n", path.c_str());
        return;
    }

    m_version = parseContext.nextLine();
    if (m_version != "IESNA91" && m_version != "IESNA:LM-63-1995" && m_version != "IESNA:LM-63-2002") {
        LogErrorAndExit("IESProfile: bad .ies file, invalid version: '%s' ('%s')\n", m_version.c_str(), path.c_str());
        return;
    }

    std::string tilt = parseContext.nextLine();
    while (tilt[0] == '[') {
        // Ignore these metadata comments
        tilt = parseContext.nextLine();
    }

    if (tilt.find("TILT=NONE") == 0) {
        m_tilt = Tilt::None;
    } else if (tilt.find("TILT=INCLUDE")) {
        m_tilt = Tilt::Include;
    } else {
        // We don't support it anyway so don't need to keep track of the file name
        m_tilt = Tilt::SpecifiedFile;
    }

    if (m_tilt != Tilt::None) {
        LogErrorAndExit("IESProfile: only TILT=NONE is supported ('%s')\n", path.c_str());
        return;
    }

    m_lampCount = parseContext.nextAsInt("# of lamps");
    if (m_lampCount <= 0) {
        LogErrorAndExit("IESProfile: bad .ies file, invalid lamp count %d' ('%s')\n", m_lampCount, path.c_str());
        return;
    } else if (m_lampCount != 1) {
        LogErrorAndExit("IESProfile: only a lamp count of 1 is supported, found %d ('%s')\n", m_lampCount, path.c_str());
        return;
    }

    m_lumensPerLamp = parseContext.nextAsFloat("lumens per lamp");

    float candelaMultiplier = parseContext.nextAsFloat("candela multiplier");
    if (candelaMultiplier <= 0.0f) {
        LogErrorAndExit("IESProfile: bad .ies file, candela multiplier must be greater than zero, found %f ('%s')\n", candelaMultiplier, path.c_str());
        return;
    }

    int numAnglesV = parseContext.nextAsInt("# of vertical angles");
    int numAnglesH = parseContext.nextAsInt("# of horizontal angles");
    int numValues = numAnglesV * numAnglesH;
    if (numValues < 1) {
        LogErrorAndExit("IESProfile: bad .ies file, number of vertical and horizontal angles must be greater than zero, found #V=%d, #H=%d ('%s')\n", numAnglesV, numAnglesH, path.c_str());
        return;
    }

    int photometricType = parseContext.nextAsInt("photometric type");
    if (photometricType == (int)PhotometricType::TypeA || photometricType == (int)PhotometricType::TypeB || photometricType == (int)PhotometricType::TypeC) {
        m_photometricType = static_cast<PhotometricType>(photometricType);
    } else {
        LogErrorAndExit("IESProfile: bad .ies file, invalid photometric type %s ('%s')\n", photometricType, path.c_str());
        return;
    }

    int unitsType = parseContext.nextAsInt("units type");
    if (unitsType == (int)UnitsType::Feet || unitsType == (int)UnitsType::Meters) {
        m_unitsType = static_cast<UnitsType>(unitsType);
    } else {
        LogErrorAndExit("IESProfile: bad .ies file, bad units type value %d ('%s')\n", unitsType, path.c_str());
        return;
    }

    m_width = parseContext.nextAsFloat("width");
    m_length = parseContext.nextAsFloat("length");
    m_height = parseContext.nextAsFloat("height");

    m_ballastFactor = parseContext.nextAsFloat("ballast factor");
    float futureUse = parseContext.nextAsFloat("future use");
    m_inputWatts = parseContext.nextAsFloat("input watts");

    m_anglesV.reserve(numAnglesV);
    float lastAngleV = -std::numeric_limits<float>::infinity();
    for (int vi = 0; vi < numAnglesV; ++vi) {
        float angle = parseContext.nextAsFloat("v angle");
        if (angle <= lastAngleV)
            LogErrorAndExit("IESProfile: bad .ies file, vertical angles should be strictly increasing ('%s')\n", path.c_str());
        m_anglesV.emplace_back(angle);
        lastAngleV = angle;
    }

    m_anglesH.reserve(numAnglesH);
    float lastAngleH = -std::numeric_limits<float>::infinity();
    for (int hi = 0; hi < numAnglesH; ++hi) {
        float angle = parseContext.nextAsFloat("h angle");
        if (angle <= lastAngleH)
            LogErrorAndExit("IESProfile: bad .ies file, horizontal angles should be strictly increasing ('%s')\n", path.c_str());
        m_anglesH.emplace_back(angle);
        lastAngleH = angle;
    }

    m_candelaValues.reserve(numValues);
    for (int i = 0; i < numValues; ++i) {
        float value = candelaMultiplier * parseContext.nextAsFloat("candela value");
        m_candelaValues.emplace_back(value);
    }
}

float IESProfile::lookupValue(float angleH, float angleV) const
{
    // NOTE: We don't realy care about the absolute orientation of these profiles here (because I'm lazy, and because it doesn't matter
    // at all when we're just applying them to arbitrary virtual light sources in a virtual scene. So in short, don't trust the relative
    // rotation of the values around the forward direction (e.g. for a spot light, the direction of it)

    vec2 lookupLocation {};
    switch (photometricType()) {

    case PhotometricType::TypeA: {
        NOT_YET_IMPLEMENTED();
        break;
    }

    case PhotometricType::TypeB: {
        NOT_YET_IMPLEMENTED();
        break;
    }

    case PhotometricType::TypeC: {

        size_t numHorizontal = m_anglesH.size();
        int lastHorizontal = static_cast<int>(std::round(m_anglesH.back()));

        if (numHorizontal == 1 && lastHorizontal == 0) {

            // "There is only one horizontal angle, implying that the luminaire is laterally symmetric in all photometric planes."
            lookupLocation = computeLookupLocation(0.0f, angleV);

        } else if (lastHorizontal == 90) {

            // "The luminaire is assumed to be symmetric in each quadrant."
            NOT_YET_IMPLEMENTED();

        } else if (lastHorizontal == 180) {

            // "The luminaire is assumed to be bilaterally symmetric about the 0-180 degree photometric plane."
            lookupLocation = { angleH, angleV };
            if (angleH >= 180.0f)
                lookupLocation.x = 180.0f - std::fmodf(angleH, 180.0f);

        } else if (lastHorizontal > 180 && lastHorizontal <= 360) {

            // "The luminaire is assumed to exhibit no lateral symmetry. (NOTE: this is an error in the draft IES LM-63-1995 standard, because
            //  the 360-degree plane is coincident with the 0-degree plane. It should read "greater than 180 degrees and less than 360 degrees")."
            lookupLocation = computeLookupLocation(angleH, angleV);

        } else {
            LogErrorAndExit("IESProfile: bad .ies file, invalid last horizontal angle value %f ('%s')\n", m_anglesH.back(), path().c_str());
        }
        break;
    }

    default:
        ASSERT_NOT_REACHED();
        break;
    }

    return getValue(lookupLocation);
}

vec2 IESProfile::computeLookupLocation(float angleH, float angleV) const
{
    auto computeScalarLookup = [](float angle, const std::vector<float>& list) -> float {
        
        ASSERT(list.size() > 0);

        int startIdx = 0;
        int endIdx = static_cast<int>(list.size() - 1);

        if (angle <= list[startIdx])
            return 0.0f;
        if (angle >= list[endIdx])
            return static_cast<float>(endIdx);

        while (startIdx < endIdx) {

            if (endIdx - startIdx == 1) {
                // Our value is between start and end, interpolate to get the right one

                float delta = list[endIdx] - list[startIdx];
                ASSERT(delta >= 0.0f);

                if (delta < 1e-3f)
                    return static_cast<float>(startIdx);

                return static_cast<float>(startIdx) + (angle - list[startIdx]) / delta;
            }

            int midIdx = (startIdx + endIdx + 1) / 2;
            float midVal = list[midIdx];

            if (angle == midVal)
                return static_cast<float>(midIdx);
            else if (angle > midVal)
                startIdx = midIdx;
            else if (angle < midVal)
                endIdx = midIdx - 1;
        }

        // We landed right on the correct value, return its index
        ASSERT(startIdx == endIdx);
        return static_cast<float>(startIdx);
    };

    return vec2(
        computeScalarLookup(angleH, m_anglesH),
        computeScalarLookup(angleV, m_anglesV));
}

float IESProfile::getValue(vec2 lookupLocation) const
{
    auto getRawValue = [this](int x, int y) -> float {
        x = moos::clamp(x, 0, int(m_anglesH.size() - 1));
        y = moos::clamp(y, 0, int(m_anglesV.size() - 1));
        return m_candelaValues[y + m_anglesV.size() * x];
    };

    int x = int(lookupLocation.x);
    int y = int(lookupLocation.y);

    float dx = lookupLocation.x - x;
    float dy = lookupLocation.y - y;

    float bl = getRawValue(x + 0, y + 0);
    float tl = getRawValue(x + 0, y + 1);
    float br = getRawValue(x + 1, y + 0);
    float tr = getRawValue(x + 1, y + 1);

    float top = moos::lerp(tl, tr, dx);
    float bot = moos::lerp(bl, br, dx);

    float value = moos::lerp(bot, top, dy);

    return value;
}
