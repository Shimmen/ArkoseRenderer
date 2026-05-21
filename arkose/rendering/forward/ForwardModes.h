#pragma once

enum class ForwardMeshFilter {
    AllMeshes,
    OnlyStaticMeshes,
    OnlySkeletalMeshes,
    OnlyHairMeshes,
};

enum class ForwardClearMode {
    DontClear,
    ClearBeforeFirstDraw,
};
