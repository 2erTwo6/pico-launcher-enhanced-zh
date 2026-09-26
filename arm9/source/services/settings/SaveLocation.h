#pragma once

/// @brief Where the launcher keeps the save file of a DS game.
enum class SaveLocation
{
    /// @brief Next to the game, as "<game name>.sav". What the loader does on its own.
    NextToRom,
    /// @brief In a "saves" folder inside the game's folder, the layout TWiLight Menu++ uses.
    SavesFolder
};
