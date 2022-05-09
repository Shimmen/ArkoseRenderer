#pragma once

// A class for the Badge pattern: https://awesomekling.github.io/Serenity-C++-patterns-The-Badge/

template<typename T>
class Badge {
private:
    friend T;
    constexpr Badge() = default;

    Badge(const Badge&) = delete;
    Badge& operator=(const Badge&) = delete;

    Badge(Badge&&) = delete;
    Badge& operator=(Badge&&) = delete;
};
