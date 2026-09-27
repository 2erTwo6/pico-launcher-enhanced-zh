#include "common.h"
#include <memory>
#include <string.h>
#include "fat/File.h"
#include "NdsRomHeader.h"

// Offsets in the rom header, as in the loader's nds_header_ntr_t / nds_header_twl_t.
#define HEADER_READ_SIZE                   0x238
#define HEADER_MAKER_CODE                  0x10
#define HEADER_UNIT_CODE                   0x12
#define UNIT_CODE_DSI_ONLY                 0x03
#define HEADER_TWL_FLAGS                   0x1C
#define HEADER_ARM7_LOAD_ADDRESS           0x38
#define HEADER_ARM9_AUTOLOAD_DONE_HOOK     0x70
#define HEADER_ARM7_AUTOLOAD_DONE_HOOK     0x74
#define HEADER_TWL_TITLE_ID                0x230

static u32 readU32(const u8* header, u32 offset)
{
    u32 value;
    memcpy(&value, &header[offset], sizeof(value));
    return value;
}

bool NdsRomHeader::UsesCardSave(const FastFileRef& romFileRef)
{
    const auto file = std::make_unique<File>();
    file->Open(romFileRef, FA_READ);

    // Heap-allocated so it doesn't live on the io task's stack.
    auto header = std::make_unique<u8[]>(HEADER_READ_SIZE);
    if (!header || !file->ReadExact(header.get(), HEADER_READ_SIZE))
    {
        LOG_ERROR("Couldn't read the rom header, keeping the save next to the game\n");
        return false;
    }

    // The loader's rule for homebrew, which it launches without a save file.
    bool homebrew = (header[HEADER_MAKER_CODE] == 0 && header[HEADER_MAKER_CODE + 1] == 0)
        || (readU32(header.get(), HEADER_ARM9_AUTOLOAD_DONE_HOOK) == 0 &&
            readU32(header.get(), HEADER_ARM7_AUTOLOAD_DONE_HOOK) == 0)
        || readU32(header.get(), HEADER_ARM7_LOAD_ADDRESS) >= 0x03000000;
    if (homebrew)
    {
        return false;
    }

    // The loader's rule for DSiWare: a twl title whose title id has the system bit set.
    bool dsiWare = (header[HEADER_TWL_FLAGS] & 1) != 0 &&
        (readU32(header.get(), HEADER_TWL_TITLE_ID + 4) & 4) != 0;
    return !dsiWare;
}

bool NdsRomHeader::IsDsiOnly(const FastFileRef& romFileRef)
{
    const auto file = std::make_unique<File>();
    file->Open(romFileRef, FA_READ);

    // 0 is a DS rom, 2 a DS rom with DSi extras, 3 a rom for the DSi only
    u8 unitCode;
    if (file->Seek(HEADER_UNIT_CODE) != FR_OK || !file->ReadExact(&unitCode, sizeof(unitCode)))
    {
        LOG_ERROR("Couldn't read the rom header, launching the game as it is\n");
        return false;
    }
    return unitCode == UNIT_CODE_DSI_ONLY;
}
