#include "common.h"
#include <libtwl/ipc/ipcFifoSystem.h>
#include "ipcChannels.h"
#include "backlightIpc.h"

/// @brief -1 until the ARM7 answers, then 1 or 0.
static volatile s8 sHasLevels = -1;

static void ipcMessageHandler(u32 channel, u32 data, void* arg)
{
    sHasLevels = data != 0 ? 1 : 0;
}

void backlight_init()
{
    ipc_setChannelHandler(IPC_CHANNEL_PMIC, ipcMessageHandler, nullptr);
    ipc_sendFifoMessage(IPC_CHANNEL_PMIC, IPC_PMIC_ASK_LEVELS);
}

bool backlight_hasLevels()
{
    return sHasLevels != 0;
}

void backlight_setLevel(unsigned int level)
{
    ipc_sendFifoMessage(IPC_CHANNEL_PMIC, level & 3);
}
