#pragma once

/// @brief Sets the DS Lite backlight level (0 = low .. 3 = max) through the
///        ARM7. Fire-and-forget: the ARM7 applies it on its main thread (the
///        PMIC shares the SPI bus with the touch screen) and only after
///        detecting a DS Lite — on the original DS the backlight register
///        mirrors the control register and must not be written. The level
///        persists into the launched game until the console powers off.
void backlight_setLevel(unsigned int level);

/// @brief Registers the reply handler and asks the ARM7 whether this console
///        has backlight levels at all. Call once after the ARM7 is up.
void backlight_init();

/// @brief Whether the console has the DS Lite's four backlight levels. True
///        until the ARM7 has answered, so nothing is hidden by mistake; false
///        on the original DS, the DSi, and a 3DS running in DSi mode.
bool backlight_hasLevels();
