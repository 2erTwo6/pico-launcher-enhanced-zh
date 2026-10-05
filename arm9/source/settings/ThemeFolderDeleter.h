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
    WriteError,
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
    /// @brief First reads the whole folder and writes nothing; only if every rule
    ///        holds, deletes it:
    ///        theme.json first, so a folder left behind never loads as a theme,
    ///        then everything else one entry at a time, then the folder itself.
    ///        Stops at the first error.
    /// @param folderName The full folder name, as the list read it from the card.
    /// @param activeTheme The theme setting in use.
    /// @param removedSomething Set once a delete has been attempted, so a failure
    ///        can say the folder may be only partly there.
    ThemeDeleteResult Delete(const char* folderName, const char* activeTheme, bool& removedSomething);
}
