#include "IESProfile.h"

#include "core/Assert.h"
#include "core/Logging.h"
#include "utility/ParseContext.h"
#include "utility/Profiling.h"
#include <ark/vector.h>
#include <half.hpp>
#include <cmath>

IESProfile::IESProfile(const std::string& path)
    : m_path(path)
{
    parse(path);
}

/*
float IESProfile::requiredSpotLightConeAngle(float minThreshold) const
{
    // This doesn't work.. at least as is. And almost all of these profiles cover a full hemisphere anyway,
    // so just assuming a large cone angle works pretty nice.
    ASSERT_NOT_REACHED();

    float maxH = 0.0f;
    float maxV = 0.0f;

    for (int hi = 0; hi < m_anglesH.size(); ++hi) {
        for (int vi = 0; vi < m_anglesV.size(); ++vi) {
            float value = m_candelaValues[vi + m_anglesV.size() * hi];
            if (value > minThreshold) {
                maxH = std::max(maxH, std::abs(m_anglesH[hi]));
                maxV = std::max(maxV, std::abs(m_anglesV[vi]));
            }
        }
    }

    // IES uses degrees for everything
    float phi = ark::toRadians(maxH);
    float theta = ark::toRadians(maxV);
    ARKOSE_ASSERT(phi >= 0.0f && phi <= ark::TWO_PI);
    ARKOSE_ASSERT(theta >= 0.0f && theta <= ark::PI);

    // Map to spherical
    float sinTheta = sin(theta);
    vec3 direction = vec3(sinTheta * cos(phi),
                          sinTheta * sin(phi),
                          cos(theta));

    // Figure out angle
    float cosHalfAngle = dot(direction, vec3(0, 0, 1));
    float angle = 2.0f * acos(cosHalfAngle);

    return angle;
}
*/

void IESProfile::parse(const std::string& path)
{
    // We should never call parse a second time
    ARKOSE_ASSERT(m_anglesV.size() == 0 && m_anglesH.size() == 0 && m_candelaValues.size() == 0);

    ParseContext parseContext { "IES", path };
    if (!parseContext.isValid()) {
        ARKOSE_LOG(Fatal, "IESProfile: could not read .ies file '{}'", path);
        return;
    }

    m_version = parseContext.nextLine();
    if (m_version != "IESNA91" && m_version != "IESNA:LM-63-1995" && m_version != "IESNA:LM-63-2002") {
        ARKOSE_LOG(Fatal, "IESProfile: bad .ies file, invalid version: '{}' ('{}')", m_version, path);
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
        ARKOSE_LOG(Fatal, "IESProfile: only TILT=NONE is supported ('{}')", path);
        return;
    }

    m_lampCount = parseContext.nextAsInt("# of lamps");
    if (m_lampCount <= 0) {
        ARKOSE_LOG(Fatal, "IESProfile: bad .ies file, invalid lamp count '{}' ('{}')", m_lampCount, path);
        return;
    } else if (m_lampCount != 1) {
        ARKOSE_LOG(Fatal, "IESProfile: only a lamp count of 1 is supported, found {} ('{}')", m_lampCount, path);
        return;
    }

    m_lumensPerLamp = parseContext.nextAsFloat("lumens per lamp");

    float candelaMultiplier = parseContext.nextAsFloat("candela multiplier");
    if (candelaMultiplier <= 0.0f) {
        ARKOSE_LOG(Fatal, "IESProfile: bad .ies file, candela multiplier must be greater than zero, found {} ('{}')", candelaMultiplier, path);
        return;
    }

    int numAnglesV = parseContext.nextAsInt("# of vertical angles");
    int numAnglesH = parseContext.nextAsInt("# of horizontal angles");
    int numValues = numAnglesV * numAnglesH;
    if (numValues < 1) {
        ARKOSE_LOG(Fatal, "IESProfile: bad .ies file, number of vertical and horizontal angles must be greater than zero, found #V={}, #H={} ('{}')", numAnglesV, numAnglesH, path);
        return;
    }

    int photometricType = parseContext.nextAsInt("photometric type");
    if (photometricType == (int)PhotometricType::TypeA || photometricType == (int)PhotometricType::TypeB || photometricType == (int)PhotometricType::TypeC) {
        m_photometricType = static_cast<PhotometricType>(photometricType);
    } else {
        ARKOSE_LOG(Fatal, "IESProfile: bad .ies file, invalid photometric type {} ('{}')", photometricType, path);
        return;
    }

    int unitsType = parseContext.nextAsInt("units type");
    if (unitsType == (int)UnitsType::Feet || unitsType == (int)UnitsType::Meters) {
        m_unitsType = static_cast<UnitsType>(unitsType);
    } else {
        ARKOSE_LOG(Fatal, "IESProfile: bad .ies file, bad units type value {} ('{}')", unitsType, path);
        return;
    }

    m_width = parseContext.nextAsFloat("width");
    m_length = parseContext.nextAsFloat("length");
    m_height = parseContext.nextAsFloat("height");

    m_ballastFactor = parseContext.nextAsFloat("ballast factor");
    [[maybe_unused]] float futureUse = parseContext.nextAsFloat("future use");
    m_inputWatts = parseContext.nextAsFloat("input watts");

    m_anglesV.reserve(numAnglesV);
    float lastAngleV = -std::numeric_limits<float>::infinity();
    for (int vi = 0; vi < numAnglesV; ++vi) {
        float angle = parseContext.nextAsFloat("v angle");
        if (angle <= lastAngleV)
            ARKOSE_LOG(Fatal, "IESProfile: bad .ies file, vertical angles should be strictly increasing ('{}')", path);
        m_anglesV.emplace_back(angle);
        lastAngleV = angle;
    }

    m_anglesH.reserve(numAnglesH);
    float lastAngleH = -std::numeric_limits<float>::infinity();
    for (int hi = 0; hi < numAnglesH; ++hi) {
        float angle = parseContext.nextAsFloat("h angle");
        if (angle <= lastAngleH)
            ARKOSE_LOG(Fatal, "IESProfile: bad .ies file, horizontal angles should be strictly increasing ('{}')", path);
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
                lookupLocation.x = 180.0f - std::fmod(angleH, 180.0f);

        } else if (lastHorizontal > 180 && lastHorizontal <= 360) {

            // "The luminaire is assumed to exhibit no lateral symmetry. (NOTE: this is an error in the draft IES LM-63-1995 standard, because
            //  the 360-degree plane is coincident with the 0-degree plane. It should read "greater than 180 degrees and less than 360 degrees")."
            lookupLocation = computeLookupLocation(angleH, angleV);

        } else {
            ARKOSE_LOG(Fatal, "IESProfile: bad .ies file, invalid last horizontal angle value {} ('{}')", m_anglesH.back(), path());
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
        
        ARKOSE_ASSERT(list.size() > 0);

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
                ARKOSE_ASSERT(delta >= 0.0f);

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
        ARKOSE_ASSERT(startIdx == endIdx);
        return static_cast<float>(startIdx);
    };

    return vec2(
        computeScalarLookup(angleH, m_anglesH),
        computeScalarLookup(angleV, m_anglesV));
}

float IESProfile::getValue(vec2 lookupLocation) const
{
    auto getRawValue = [this](int x, int y) -> float {
        x = ark::clamp(x, 0, int(m_anglesH.size() - 1));
        y = ark::clamp(y, 0, int(m_anglesV.size() - 1));
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

    float top = ark::lerp(tl, tr, dx);
    float bot = ark::lerp(bl, br, dx);

    float value = ark::lerp(bot, top, dy);

    return value;
}

template<typename T>
std::vector<T> IESProfile::assembleLookupTextureData(u32 lutSize) const
{
    std::vector<T> pixels {};
    pixels.reserve(lutSize * lutSize);

    for (u32 y = 0; y < lutSize; ++y) {
        float horizontal = y / static_cast<float>(lutSize) * 360.0f;
        for (u32 x = 0; x < lutSize; ++x) {
            float vertical = x / static_cast<float>(lutSize) * 180.0f;

            T value = static_cast<T>(lookupValue(horizontal, vertical));
            pixels.push_back(value);
        }
    }

    return pixels;
}

template std::vector<float> IESProfile::assembleLookupTextureData(u32) const;
template std::vector<half_float::half> IESProfile::assembleLookupTextureData(u32) const;
