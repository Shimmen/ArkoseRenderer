#ifndef DDGI_H
#define DDGI_H

#define DDGI_IRRADIANCE_RES (8)
#define DDGI_VISIBILITY_RES (16)

#define DDGI_ATLAS_TILE_BORDER   (1)
#define DDGI_ATLAS_TILE_DISTANCE (0) // note: must be zero now.. because border copy calculations
#define DDGI_ATLAS_PADDING (DDGI_ATLAS_TILE_BORDER + DDGI_ATLAS_TILE_DISTANCE)

struct DDGIProbeGridData {
    ivec4 gridDimensions;
    vec4 probeSpacing;
    vec4 offsetToFirst;
};

#endif // DDGI_H
