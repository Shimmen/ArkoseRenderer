#version 460

#include <common.glsl>
#include <common/camera.glsl>

// Shamelessly inspired by / copied from https://github.com/emeiri/ogldev/blob/master/DemoLITION/Framework/Shaders/GL/infinite_grid.fs
// by Etay Meiri. Copyright 2024 Etay Meiri under the GNU General Public License version 3.

const float minGridCellSize = 0.01; // 1cm -> 10cm -> 100cm -> ...
const float minPixelsBetweenCells = 2.0;

const vec4 gridColorThin = vec4(0.6, 0.6, 0.6, 1.0);
const vec4 gridColorThick = vec4(1.0, 1.0, 1.0, 1.0);

layout(set = 0, binding = 0) uniform CameraStateBlock { CameraState camera; };

layout(location = 0) in vec3 vPosition;
layout(location = 1) in float vRadius;

layout(location = 0) out vec4 oColor;

float log10(float x)
{
    return log(x) / log(10.0);
}

float doGrid(vec3 position, float gridCellSize, vec2 dP)
{
    vec2 positionModCell = mod(position.xz, gridCellSize) / dP;
    return maxComponent(1.0 - abs(saturate(positionModCell) * 2.0 - 1.0) );
}

void main()
{
    vec2 dPx = vec2(dFdx(vPosition.x), dFdy(vPosition.x));
    vec2 dPy = vec2(dFdx(vPosition.z), dFdy(vPosition.z));
    vec2 dP = vec2(length(dPx), length(dPy));

    // Calculate grid sizes
    float lod = max(0.0, log10(length(dP) * minPixelsBetweenCells / minGridCellSize) + 1.0);
    float gridCellSizeLod0 = minGridCellSize  * pow(10.0, floor(lod));
    float gridCellSizeLod1 = gridCellSizeLod0 * 10.0;
    float gridCellSizeLod2 = gridCellSizeLod1 * 10.0;

    const float lineThicknessFactor = 4.0;
    dP *= lineThicknessFactor;

    // Figure out where to draw each grid LOD
    float gridLod0 = doGrid(vPosition, gridCellSizeLod0, dP);
    float gridLod1 = doGrid(vPosition, gridCellSizeLod1, dP);
    float gridLod2 = doGrid(vPosition, gridCellSizeLod2, dP);
    float lodFade = fract(lod);

    // Combine grids & calculate colors
    vec4 gridColor;
    if (gridLod2 > 0.0) {
        gridColor = gridColorThick;
        gridColor.a *= gridLod2;
    } else {
        if (gridLod1 > 0.0) {
            gridColor = mix(gridColorThick, gridColorThin, lodFade);
            gridColor.a *= gridLod1;
        } else {
            gridColor = gridColorThin;
            gridColor.a *= gridLod0 * (1.0 - lodFade);
        }
    }

    // Distance falloff
    float distanceFalloff = 1.0 - saturate(distance(vPosition.xz, camera_getPosition(camera).xz) / vRadius);
    gridColor.a *= distanceFalloff;

    oColor = gridColor;
}
