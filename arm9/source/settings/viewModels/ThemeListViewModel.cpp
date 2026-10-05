#include "common.h"
#include "settings/ISettingsController.h"
#include "themes/ThemeRepository.h"
#include "ThemeListViewModel.h"

ThemeListViewModel::ThemeListViewModel(ISettingsController* settingsController, const char* activeThemeFolderName)
    : _settingsController(settingsController)
{
    // after a delete the selector restarts on the theme that took the deleted
    // one's place; otherwise it opens on the theme in use
    int reopenIndex = settingsController->TakeReopenIndex();
    int themeCount = (int)settingsController->GetThemeRepository().GetThemeCount();
    if (reopenIndex >= 0 && themeCount > 0)
    {
        _selectedItem = reopenIndex < themeCount ? reopenIndex : themeCount - 1;
    }
    else
    {
        _selectedItem = settingsController->GetThemeRepository().FindThemeIndex(activeThemeFolderName);
    }
}

void ThemeListViewModel::NavigateUp() const
{
    _settingsController->NavigateUp();
}

bool ThemeListViewModel::CanDeleteSelected() const
{
    return _settingsController->CanDeleteTheme(_selectedItem);
}

void ThemeListViewModel::RequestDeleteSelected() const
{
    _settingsController->RequestDeleteTheme(_selectedItem);
}
