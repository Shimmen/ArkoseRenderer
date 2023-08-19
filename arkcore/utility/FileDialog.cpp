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

    return nfdFilterItems;
}

std::optional<std::string> open(std::vector<FilterItem> filterItems, std::string_view defaultPath)
{
    if (NFD_Init() != NFD_OKAY) {
        ARKOSE_LOG_FATAL("Failed to init NFD");
    }

    std::optional<std::string> result {};

    nfdchar_t* nfdOutPath;
    auto nfdFilterItems = translateFilterItems(filterItems);
    if (NFD_OpenDialog(&nfdOutPath, nfdFilterItems.data(), static_cast<nfdfiltersize_t>(nfdFilterItems.size()), defaultPath.data()) == NFD_OKAY) {
        result = std::string(nfdOutPath);
        NFD_FreePath(nfdOutPath);
    } else if (const char* error = NFD_GetError()) {
        ARKOSE_LOG(Error, "Open file dialog error: {}.", error);
        NFD_ClearError();
    }

    NFD_Quit();

    return result;
}

std::vector<std::string> openMultiple(std::vector<FilterItem> filterItems, std::string_view defaultPath)
{
    if (NFD_Init() != NFD_OKAY) {
        ARKOSE_LOG_FATAL("Failed to init NFD");
    }

    std::vector<std::string> result {};

    nfdpathset_t const* nfdPathSet;
    auto nfdFilterItems = translateFilterItems(filterItems);
    if (NFD_OpenDialogMultiple(&nfdPathSet, nfdFilterItems.data(), static_cast<nfdfiltersize_t>(nfdFilterItems.size()), defaultPath.data()) == NFD_OKAY) {

        nfdpathsetenum_t nfdPathSetEnumerator;
        NFD_PathSet_GetEnum(nfdPathSet, &nfdPathSetEnumerator);

        nfdchar_t* nfdPath;
        while (NFD_PathSet_EnumNext(&nfdPathSetEnumerator, &nfdPath) && nfdPath != nullptr) {
            result.emplace_back(nfdPath);
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

std::optional<std::string> save(std::vector<FilterItem> filterItems, std::string_view defaultPath, std::string_view defaultName)
{
    if (NFD_Init() != NFD_OKAY) {
        ARKOSE_LOG_FATAL("Failed to init NFD");
    }

    std::optional<std::string> result {};

    nfdchar_t* nfdSavePath;
    auto nfdFilterItems = translateFilterItems(filterItems);
    if (NFD_SaveDialog(&nfdSavePath, nfdFilterItems.data(), static_cast<nfdfiltersize_t>(nfdFilterItems.size()), defaultPath.data(), defaultName.data()) == NFD_OKAY) {
        result = std::string(nfdSavePath);
        NFD_FreePath(nfdSavePath);
    } else if (const char* error = NFD_GetError()) {
        ARKOSE_LOG(Error, "Save file dialog error: {}.", error);
        NFD_ClearError();
    }

    NFD_Quit();

    return result;
}

} // namespace FileDialog
