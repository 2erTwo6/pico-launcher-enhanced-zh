#include "common.h"
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
    gProcessManager.Goto<App>();
}

void SettingsController::SelectTheme(const char* themeFolderName)
{
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

    LOG_DEBUG("Delete requested for theme '%s'\n", _themeRepository.GetThemeFolderName(themeIndex));
}
