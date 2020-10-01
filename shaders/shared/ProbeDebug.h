#ifndef PROBE_DEBUG_H
#define PROBE_DEBUG_H

#define PROBE_DEBUG_VISUALIZE_COLOR     0
#define PROBE_DEBUG_VISUALIZE_DISTANCE  1
#define PROBE_DEBUG_VISUALIZE_DISTANCE2 2

// What should be shown by DiffuseGIProbeDebug?
#define PROBE_DEBUG_VIZ PROBE_DEBUG_VISUALIZE_COLOR

// Should we render to high res probes to make visualization easier?
// To save your GPU from crashing, this will also make the irradiance
// visualization be radiance instead, so we don't have to do expensive
// filtering slowing down everything immensely for large textures.
#define PROBE_DEBUG_HIGH_RES_VIZ 0

#endif // PROBE_DEBUG_H
