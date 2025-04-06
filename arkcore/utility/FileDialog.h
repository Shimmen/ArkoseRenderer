#pragma once

#include <filesystem>
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

std::optional<std::filesystem::path> open(std::vector<FilterItem>, std::filesystem::path defaultPath = {});
std::vector<std::filesystem::path> openMultiple(std::vector<FilterItem>, std::filesystem::path defaultPath = {});

std::optional<std::filesystem::path> save(std::vector<FilterItem>, std::filesystem::path defaultPath = {}, std::string_view defaultName = {});

}
