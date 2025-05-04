#include "FileDialog.h"

#include "core/Logging.h"

#include <nfd.h> // (native file dialog)

namespace FileDialog {

static std::vector<nfdfilteritem_t> translateFilterItems(std::vector<FilterItem> const& filterItems)
{
    std::vector<nfdfilteritem_t> nfdFilterItems {};

    for (FilterItem const& filterItem : filterItems) {
        nfdFilterItems.push_back({ .name = filterItem.name,
                                   .spec = filterItem.extensions });
    }

    for (nfdfilteritem_t& filterItem : nfdFilterItems) {
        // Skip any leading dots of the extension, as NDF doesn't want them..
        if (filterItem.spec && filterItem.spec[0] == '.') {
            filterItem.spec += 1;
        }
    }

    return nfdFilterItems;
}

std::optional<std::filesystem::path> open(std::vector<FilterItem> filterItems, std::filesystem::path defaultPath)
{
    if (NFD_Init() != NFD_OKAY) {
        ARKOSE_LOG(Fatal, "Failed to init NFD");
    }

    std::optional<std::filesystem::path> result {};

    nfdchar_t* nfdOutPath;
    auto nfdFilterItems = translateFilterItems(filterItems);
    if (NFD_OpenDialog(&nfdOutPath, nfdFilterItems.data(), static_cast<nfdfiltersize_t>(nfdFilterItems.size()), defaultPath.string().c_str()) == NFD_OKAY) {
        result = std::filesystem::path(nfdOutPath);
        NFD_FreePath(nfdOutPath);
    } else if (const char* error = NFD_GetError()) {
        ARKOSE_LOG(Error, "Open file dialog error: {}.", error);
        NFD_ClearError();
    }

    NFD_Quit();

    return result;
}

std::vector<std::filesystem::path> openMultiple(std::vector<FilterItem> filterItems, std::filesystem::path defaultPath)
{
    if (NFD_Init() != NFD_OKAY) {
        ARKOSE_LOG(Fatal, "Failed to init NFD");
    }

    std::vector<std::filesystem::path> result {};

    nfdpathset_t const* nfdPathSet;
    auto nfdFilterItems = translateFilterItems(filterItems);
    if (NFD_OpenDialogMultiple(&nfdPathSet, nfdFilterItems.data(), static_cast<nfdfiltersize_t>(nfdFilterItems.size()), defaultPath.string().c_str()) == NFD_OKAY) {

        nfdpathsetenum_t nfdPathSetEnumerator;
        NFD_PathSet_GetEnum(nfdPathSet, &nfdPathSetEnumerator);

        nfdchar_t* nfdPath;
        while (NFD_PathSet_EnumNext(&nfdPathSetEnumerator, &nfdPath) && nfdPath != nullptr) {
            result.emplace_back(std::filesystem::path(nfdPath));
            NFD_PathSet_FreePath(nfdPath);
        }

        NFD_PathSet_FreeEnum(&nfdPathSetEnumerator);
        NFD_PathSet_Free(nfdPathSet);

    } else if (const char* error = NFD_GetError()) {
        ARKOSE_LOG(Error, "Open file dialog error: {}.", error);
        NFD_ClearError();
    }

    NFD_Quit();

    return result;
}

std::optional<std::filesystem::path> save(std::vector<FilterItem> filterItems, std::filesystem::path defaultPath, std::string_view defaultName)
{
    if (NFD_Init() != NFD_OKAY) {
        ARKOSE_LOG(Fatal, "Failed to init NFD");
    }

    std::optional<std::filesystem::path> result {};

    nfdchar_t* nfdSavePath;
    auto nfdFilterItems = translateFilterItems(filterItems);
    if (NFD_SaveDialog(&nfdSavePath, nfdFilterItems.data(), static_cast<nfdfiltersize_t>(nfdFilterItems.size()), defaultPath.string().c_str(), defaultName.data()) == NFD_OKAY) {
        result = std::filesystem::path(nfdSavePath);
        NFD_FreePath(nfdSavePath);
    } else if (const char* error = NFD_GetError()) {
        ARKOSE_LOG(Error, "Save file dialog error: {}.", error);
        NFD_ClearError();
    }

    NFD_Quit();

    return result;
}

} // namespace FileDialog
