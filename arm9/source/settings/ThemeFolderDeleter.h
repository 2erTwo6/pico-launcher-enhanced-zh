#pragma once
#include "common.h"

enum class ThemeDeleteResult
{
    Ok,
    /// The listed folder is not there, or the path opens some other entry.
    NotFound,
    /// It is, or turns out to be, the theme in use or one that comes with the launcher.
    Protected,
    ReadOnly,
    TooDeep,
    TooManyEntries,
    PathTooLong,
    BadName,
    ReadError,
    OutOfMemory
};

struct ThemeDeleteCounts
{
    u32 files = 0;
    u32 folders = 0;
};

/// @brief Checks, and later deletes, one theme folder: /_pico/themes/<name> and
///        nothing outside it. Runs on the IO thread; it never touches the card
///        from the main thread.
namespace ThemeFolderDeleter
{
    /// @brief Pass 1. Reads the whole folder and writes nothing. Ok means every
    ///        rule held, so deleting it can only remove what was counted.
    /// @param folderName The full folder name, as the list read it from the card.
    /// @param activeTheme The theme setting in use.
    ThemeDeleteResult Check(const char* folderName, const char* activeTheme, ThemeDeleteCounts& counts);
}
