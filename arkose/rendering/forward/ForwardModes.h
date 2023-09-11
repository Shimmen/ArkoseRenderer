#pragma once

enum class ForwardMeshFilter {
    AllMeshes,
    OnlyStaticMeshes,
    OnlySkeletalMeshes,
};

enum class ForwardClearMode {
    DontClear,
    ClearBeforeFirstDraw,
};
