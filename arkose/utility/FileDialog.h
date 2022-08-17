#pragma once

#include <optional>
#include <vector>
#include <string>

namespace FileDialog {

struct FilterItem {
    // Display name for filter item in file dialog
    const char* name;
    // Filter file extensions, comma separated (e.g. "exe,lib,dll")
    const char* extensions;
};

std::optional<std::string> open(std::vector<FilterItem>, std::string_view defaultPath = {});
std::optional<std::string> save(std::vector<FilterItem>, std::string_view defaultPath = {}, std::string_view defaultName = {});

}
