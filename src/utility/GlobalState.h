#pragma once

#include "Extent.h"

class Backend;

class GlobalState {
public:
    static const GlobalState& get();
    static GlobalState& getMutable();

    [[nodiscard]] Extent2D windowExtent() const;
    void updateWindowExtent(const Extent2D&);

    [[nodiscard]] bool guiIsUsingTheMouse() const;
    [[nodiscard]] bool guiIsUsingTheKeyboard() const;

private:
    Extent2D m_windowExtent {};
};
