#ifndef KHRONOS_PBR_NEUTRAL_GLSL
#define KHRONOS_PBR_NEUTRAL_GLSL

//
// Implementation of the "Khronos PBR Neutral Tone Mapper"
// https://github.com/KhronosGroup/ToneMapping/tree/main/PBR_Neutral
//

// The function `PBRNeutralToneMapping` between the v and ^ markers below is the exact reference implementation by Khronos Group,
// licenced under Apache License 2.0. See https://github.com/KhronosGroup/ToneMapping/blob/main/PBR_Neutral/pbrNeutral.glsl
// vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv

// Input color is non-negative and resides in the Linear Rec. 709 color space.
// Output color is also Linear Rec. 709, but in the [0, 1] range.

vec3 PBRNeutralToneMapping( vec3 color ) {
  const float startCompression = 0.8 - 0.04;
  const float desaturation = 0.15;

  float x = min(color.r, min(color.g, color.b));
  float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
  color -= offset;

  float peak = max(color.r, max(color.g, color.b));
  if (peak < startCompression) return color;

  const float d = 1. - startCompression;
  float newPeak = 1. - d * d / (peak + d - startCompression);
  color *= newPeak / peak;

  float g = 1. - 1. / (desaturation * (peak - newPeak) + 1.);
  return mix(color, newPeak * vec3(1, 1, 1), g);
}

// ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

vec3 khronosPbrNeutralTonemap(vec3 color)
{
    color = max(color, vec3(0.0));
    return PBRNeutralToneMapping(color);
}

#endif // KHRONOS_PBR_NEUTRAL_GLSL
