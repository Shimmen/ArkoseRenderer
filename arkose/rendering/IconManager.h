#pragma once

class Backend;

#include "rendering/Icon.h"
#include "rendering/backend/base/Texture.h"
#include <memory>

class IconManager final {
public:

    explicit IconManager(Backend&);
    ~IconManager();

    Icon const& lightbulb() const { return m_lightbulbIcon; }

private:
    Icon loadIcon(Backend&, std::string_view iconName);

    // Common icons
    Icon m_lightbulbIcon {};
};
