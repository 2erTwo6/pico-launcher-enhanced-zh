#pragma once
#include "fat/FastFileRef.h"

/// @brief Reads the part of an nds rom header that says where its save lives.
class NdsRomHeader
{
public:
    /// @brief Returns whether the loader keeps a save file on the card for the rom, at the path
    ///        the launcher gives it. It does not for homebrew, by the loader's own rule, nor for
    ///        DSiWare, whose save files it derives from the rom path itself.
    /// @param romFileRef The rom.
    /// @return \c true when the loader uses the launcher's save path, or \c false otherwise,
    ///         including when the header can't be read.
    static bool UsesCardSave(const FastFileRef& romFileRef);
};
