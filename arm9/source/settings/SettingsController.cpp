#include "common.h"
#include <string.h>
#include <strings.h>
#include "App.h"
#include "SettingsProcess.h"
#include "ThemeFolderRules.h"
#include "SettingsController.h"

/// The list position the selector reopens on after restarting for a delete.
/// Survives the process restart, which is why it is static; read once.
static int sReopenIndex = -1;

SettingsController::SettingsController(IAppSettingsService* appSettingsService, TaskQueueBase* ioTaskQueue)
    : _appSettingsService(appSettingsService), _ioTaskQueue(ioTaskQueue) { }

void SettingsController::Initialize()
{
    _themeRepository.Initialize();
    _themeInfoManager = std::make_unique<ThemeInfoManager>(_themeRepository);
}

void SettingsController::NavigateUp()
{
    // not while a delete sheet is up or its delete runs; the sheet takes B then
    if (_deleteState != ThemeDeleteState::None)
    {
        return;
    }

    gProcessManager.Goto<App>();
}

void SettingsController::SelectTheme(const char* themeFolderName)
{
    if (_deleteState != ThemeDeleteState::None)
    {
        return;
    }

    _appSettingsService->GetAppSettings().theme = themeFolderName;
    _ioTaskQueue->Enqueue([this] (const vu8& cancelRequested)
    {
        _appSettingsService->Save();
        return TaskResult<void>::Completed();
    });
    gProcessManager.Goto<App>();
}

bool SettingsController::CanDeleteTheme(int themeIndex) const
{
    const TCHAR* folderName = _themeRepository.GetThemeFolderName(themeIndex);
    if (!ThemeFolderRules::IsSafeName(folderName, false) || ThemeFolderRules::IsProtected(folderName))
    {
        return false;
    }

    // The setting is used as a path segment, and FatFs cuts or follows some names
    // ("x ", "x.", "x/", "./x"), so a setting that is not a plain name can open a
    // folder no listed name equals. Then nothing may be deleted.
    const char* activeTheme = _appSettingsService->GetAppSettings().theme.GetString();
    if (!ThemeFolderRules::IsSafeName(activeTheme, false))
    {
        return false;
    }

    return strcasecmp(folderName, activeTheme) != 0;
}

void SettingsController::RequestDeleteTheme(int themeIndex)
{
    // the button's dimming is not a guard: a tap that started while it was lit
    // can end after it went dim, so every request is checked again here
    if (!CanDeleteTheme(themeIndex))
    {
        return;
    }

    if (_deleteState != ThemeDeleteState::None)
    {
        return;
    }

    // Copy everything the sheet and the delete will use now, so a list that
    // scrolls or reloads behind the sheet can't change what gets deleted.
    const TCHAR* folderName = _themeRepository.GetThemeFolderName(themeIndex);
    size_t folderNameLength = strlen(folderName);
    if (folderNameLength > FF_LFN_BUF)
    {
        return;
    }
    memcpy(_deleteFolderName, folderName, folderNameLength + 1);

    _deleteThemeName[0] = 0;
    auto extraThemeInfo = _themeInfoManager->GetExtraThemeInfo(themeIndex);
    if (extraThemeInfo && extraThemeInfo->themeInfo)
    {
        const char16_t* name = extraThemeInfo->themeInfo->GetName();
        u32 i = 0;
        for (; name[i] != 0 && i < sizeof(_deleteThemeName) / sizeof(_deleteThemeName[0]) - 1; i++)
        {
            _deleteThemeName[i] = name[i];
        }
        _deleteThemeName[i] = 0;
    }

    const char* activeTheme = _appSettingsService->GetAppSettings().theme.GetString();
    size_t activeThemeLength = strlen(activeTheme);
    if (activeThemeLength >= sizeof(_deleteActiveTheme))
    {
        return;
    }
    memcpy(_deleteActiveTheme, activeTheme, activeThemeLength + 1);

    _deleteIndex = themeIndex;
    _deleteStatus = nullptr;
    _deleteState = ThemeDeleteState::Confirming;
    LOG_DEBUG("Delete requested for theme '%s'\n", _deleteFolderName);
}

void SettingsController::ConfirmDeleteTheme()
{
    if (_deleteState != ThemeDeleteState::Confirming)
    {
        return;
    }

    _deleteState = ThemeDeleteState::Deleting;
    _deleteTaskDone = false;
    _deleteStatus = "Deleting...";
    // Captures only `this`: the task slot is small, and everything the task
    // reads was copied into this controller when the delete was asked for.
    _ioTaskQueue->Enqueue([this] (const vu8& cancelRequested)
    {
        _deleteResult = ThemeFolderDeleter::Delete(_deleteFolderName, _deleteActiveTheme, _deleteRemovedSomething);
        // the result is written before the flag that says it is there
        asm volatile("" ::: "memory");
        _deleteTaskDone = true;
        return TaskResult<void>::Completed();
    });
}

void SettingsController::UpdateDeleteTheme()
{
    if (_deleteState != ThemeDeleteState::Deleting || !_deleteTaskDone)
    {
        return;
    }
    asm volatile("" ::: "memory");
    SetDeleteResultStatus();
    _deleteState = ThemeDeleteState::Finished;
    if (_deleteResult == ThemeDeleteResult::Ok)
    {
        // the list is read once, when the selector starts, so it starts again
        RestartSelector();
    }
    else
    {
        // if part of it went, the list must show what is left once the
        // reason has been read
        _restartAfterResult = _deleteRemovedSomething;
    }
}

void SettingsController::RestartSelector()
{
    // the theme that moved into the deleted one's place, clamped when the list
    // gets read again
    sReopenIndex = _deleteIndex;
    gProcessManager.Goto<SettingsProcess>();
}

int SettingsController::TakeReopenIndex()
{
    int index = sReopenIndex;
    sReopenIndex = -1;
    return index;
}

void SettingsController::SetDeleteResultStatus()
{
    switch (_deleteResult)
    {
        case ThemeDeleteResult::Ok:
            _deleteStatus = "Deleted";
            return;
        case ThemeDeleteResult::NotFound:
            _deleteStatus = "Couldn't find that theme's folder";
            break;
        case ThemeDeleteResult::Protected:
            _deleteStatus = "That theme can't be deleted";
            break;
        case ThemeDeleteResult::ReadOnly:
            _deleteStatus = "Couldn't delete: a file is read-only";
            break;
        case ThemeDeleteResult::TooDeep:
            _deleteStatus = "Couldn't delete: too many folders deep";
            break;
        case ThemeDeleteResult::TooManyEntries:
            _deleteStatus = "Couldn't delete: too many files";
            break;
        case ThemeDeleteResult::PathTooLong:
            _deleteStatus = "Couldn't delete: a name is too long";
            break;
        case ThemeDeleteResult::BadName:
            _deleteStatus = "Couldn't delete: a name can't be read";
            break;
        case ThemeDeleteResult::ReadError:
            _deleteStatus = "Couldn't read the card";
            break;
        case ThemeDeleteResult::WriteError:
            _deleteStatus = "Couldn't write to the card";
            break;
        case ThemeDeleteResult::OutOfMemory:
            _deleteStatus = "Not enough memory, try again";
            break;
    }
    if (_deleteRemovedSomething)
    {
        // whatever the reason, the folder is now only partly there
        _deleteStatus = "Couldn't delete it all, try again";
    }
}

void SettingsController::CancelDeleteTheme()
{
    if (_deleteState == ThemeDeleteState::Confirming)
    {
        _deleteState = ThemeDeleteState::None;
    }
}

void SettingsController::EndDeleteTheme()
{
    if (_deleteState != ThemeDeleteState::Finished)
    {
        return;
    }
    if (_restartAfterResult)
    {
        // stays Finished, so nothing can be picked while the selector fades out;
        // the process calls this once, when the result has been shown
        _restartAfterResult = false;
        RestartSelector();
        return;
    }
    _deleteStatus = nullptr;
    _deleteState = ThemeDeleteState::None;
}
