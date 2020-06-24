#pragma once

#include <mooslib/vector.h>

class Material {
public:
    std::string baseColor {};
    vec4 baseColorFactor { 1.0f };

    std::string normalMap {};
    std::string metallicRoughness {};
    std::string emissive {};
};
