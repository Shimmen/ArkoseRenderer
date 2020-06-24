#pragma once

//! A class for the Badge pattern: https://awesomekling.github.io/Serenity-C++-patterns-The-Badge/
template<typename T>
class Badge {
private:
    friend T;
    Badge() { }

public:
    Badge(Badge&) { }
};
