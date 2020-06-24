#ifndef SPHERICAL_HARMONICS_H
#define SPHERICAL_HARMONICS_H

// FIXME: This can probably be packed more efficiently, since we only care about the rgb's
struct SphericalHarmonics {
    vec4 L00;
    vec4 L11;
    vec4 L10;
    vec4 L1_1;
    vec4 L21;
    vec4 L2_1;
    vec4 L2_2;
    vec4 L20;
    vec4 L22;
};

#endif // SPHERICAL_HARMONICS_H
