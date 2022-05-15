#pragma once

namespace conversion {

namespace constants {
constexpr size_t BytesToKilobytes = (1 << 10);
constexpr size_t BytesToMegabytes = (1 << 20);
constexpr size_t BytesToGigabytes = (1 << 30);
}

// from bytes to ...
namespace to {
    template<typename InT, typename OutT = float>
    constexpr OutT KB(InT bytes)
    {
        return static_cast<OutT>(bytes) / static_cast<OutT>(constants::BytesToKilobytes);
    }

    template<typename InT, typename OutT = float>
    constexpr OutT MB(InT bytes)
    {
        return static_cast<OutT>(bytes) / static_cast<OutT>(constants::BytesToMegabytes);
    }

    template<typename InT, typename OutT = float>
    constexpr OutT GB(InT bytes)
    {
        return static_cast<OutT>(bytes) / static_cast<OutT>(constants::BytesToGigabytes);
    }
}

}
