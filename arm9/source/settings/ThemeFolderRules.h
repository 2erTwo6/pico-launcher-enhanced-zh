#pragma once
#include <string.h>
#include <strings.h>

/// @brief What a theme folder has to be before the theme selector offers to
///        delete it. Shared by the delete button and the code that deletes, so
///        the two can never disagree.
namespace ThemeFolderRules
{
    /// @brief The themes that come with the launcher. The selector never deletes
    ///        them: material is the default theme, and both come back with the
    ///        next update anyway.
    inline bool IsProtected(const char* folderName)
    {
        return strcasecmp(folderName, "material") == 0 || strcasecmp(folderName, "raspberry") == 0;
    }

    /// @brief A name FatFs parses as exactly one path segment with nothing cut
    ///        off: not "." or "..", no trailing dot or space (FatFs strips them,
    ///        so "material." opens "material"), no separator or character FatFs
    ///        rejects, and not the "?" it reports for a name it cannot read.
    ///        It does not prove the name opens its own entry: with code page 437 a
    ///        short name with accents ("matérial") can open another folder
    ///        ("material") through its 8.3 name, so the code that deletes must
    ///        also check on the card that it opened the listed entry.
    /// @param allowLeadingDot True for entries inside a theme folder, where ._ and
    ///        .DS_Store files are ordinary files to delete. A theme folder itself
    ///        starting with a dot is hidden from the list, so it never gets here.
    inline bool IsSafeName(const char* name, bool allowLeadingDot)
    {
        if (name == nullptr)
            return false;
        size_t length = strlen(name);
        if (length == 0 || length > 255)
            return false;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
            return false;
        if (!allowLeadingDot && name[0] == '.')
            return false;
        char last = name[length - 1];
        if (last == ' ' || last == '.')
            return false;
        for (size_t i = 0; i < length; i++)
        {
            unsigned char c = name[i];
            if (c < 0x20 || c == 0x7F || strchr("/\\:*?\"<>|", c) != nullptr)
                return false;
        }
        return true;
    }
}
