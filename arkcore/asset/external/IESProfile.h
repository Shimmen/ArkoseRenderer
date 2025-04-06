#pragma once

#include "core/Types.h"
#include <memory>
#include <string>
#include <vector>
#include <ark/vector.h>
#include <cereal/cereal.hpp>

class Backend;
class Texture;

class IESProfile {
public:

    // Implementation of the IES specification: http://lumen.iee.put.poznan.pl/kw/iesna.txt
    // Not actually up to spec though, but it works for many sample files & is usable enough.

    enum class Tilt {
        None,
        Include,
        SpecifiedFile,
    };

    enum class PhotometricType {
        TypeC = 1,
        TypeB = 2,
        TypeA = 3
    };

    enum class UnitsType {
        Feet = 1,
        Meters = 2,
    };

    IESProfile() = default;
    IESProfile(const std::string& path);

    IESProfile(IESProfile&) = delete;
    IESProfile& operator=(IESProfile&) = delete;

    void load(std::filesystem::path const& path)
    {
        m_path = path;
        parse(m_path);
    }

    std::filesystem::path const& path() const { return m_path; }
    UnitsType unitsType() const { return m_unitsType; }
    PhotometricType photometricType() const { return m_photometricType; }

    //float requiredSpotLightConeAngle(float minThreshold) const;

    float lookupValue(float angleH, float angleV) const;

    template<typename T>
    std::vector<T> assembleLookupTextureData(u32 lutSize) const;

    template<class Archive>
    std::string save_minimal(Archive const&) const
    {
        return m_path.string();
    }

    template<class Archive>
    void load_minimal(Archive const&, std::string const& value)
    {
        m_path = value;
        parse(m_path);
    }

private:

    void parse(std::filesystem::path const& path);
    std::filesystem::path m_path;

    vec2 computeLookupLocation(float angleH, float angleV) const;
    float getValue(vec2 lookupLocation) const;

    std::string m_version;

    Tilt m_tilt { Tilt::None };

    UnitsType m_unitsType { UnitsType::Feet };
    PhotometricType m_photometricType { PhotometricType::TypeA };

    int m_lampCount {};
    float m_lumensPerLamp {};

    float m_width {};
    float m_length {};
    float m_height {};

    float m_ballastFactor {};
    float m_inputWatts {};

    std::vector<float> m_anglesV {};
    std::vector<float> m_anglesH {};
    std::vector<float> m_candelaValues {};

};
