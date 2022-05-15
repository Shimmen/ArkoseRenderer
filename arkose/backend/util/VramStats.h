#pragma once

#include <vector>

struct VramStats {
    struct MemoryHeap {
        size_t used { 0 };
        size_t available { 0 };
        bool deviceLocal { false };
        bool hostVisible { false };
        bool hostCoherent { false };
    };

    std::vector<MemoryHeap> heaps {};
    size_t totalUsed { 0 };
};
