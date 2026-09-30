#include "common.h"
#include <string.h>
#include <strings.h>
#include "App.h"
#include "ThemeFolderRules.h"
#include "SettingsController.h"

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

    // test build: nothing is deleted yet
    _deleteStatus = "Nothing was deleted (test build)";
    _deleteState = ThemeDeleteState::Finished;
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
    if (_deleteState == ThemeDeleteState::Finished)
    {
        _deleteStatus = nullptr;
        _deleteState = ThemeDeleteState::None;
    }
}
