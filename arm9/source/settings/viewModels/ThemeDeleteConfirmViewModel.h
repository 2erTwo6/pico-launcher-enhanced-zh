#pragma once
#include "core/mini-printf.h"
#include "core/String.h"
#include "fat/ff.h"
#include "romBrowser/viewModels/IDeleteConfirmViewModel.h"
#include "settings/ISettingsController.h"

/// @brief The delete sheet for a theme. The list shows theme names, and two
///        themes can share one, so the sheet names the folder too. Copies its
///        texts when it is built, so nothing dangles while it animates.
class ThemeDeleteConfirmViewModel : public IDeleteConfirmViewModel
{
public:
    explicit ThemeDeleteConfirmViewModel(ISettingsController* settingsController)
        : _settingsController(settingsController)
        , _themeName(settingsController->GetDeleteThemeName())
        , _folderName(settingsController->GetDeleteFolderName())
    {
        // display only: a cut-off folder name here ends in an ellipsis on screen
        mini_snprintf(_detailLine, sizeof(_detailLine), "The folder %s is deleted, with all its files",
            _folderName.GetString());
    }

    const char16_t* GetTitle() const override { return u"Delete theme?"; }

    const char16_t* GetNameLine16() const override
    {
        // a theme without a name, or a folder with no readable theme.json,
        // is named by its folder instead
        return _themeName.GetString()[0] != 0 ? _themeName.GetString() : nullptr;
    }

    const char* GetNameLine() const override { return _folderName.GetString(); }
    const char* GetDetailLine() const override { return _detailLine; }
    const char* GetStatusLine() const override { return _settingsController->GetDeleteStatus(); }

    void Confirm() override { _settingsController->ConfirmDeleteTheme(); }
    void Cancel() override { _settingsController->CancelDeleteTheme(); }

private:
    ISettingsController* _settingsController;
    String<char16_t, 65> _themeName;
    String<char, FF_LFN_BUF + 1> _folderName;
    char _detailLine[FF_LFN_BUF + 64];
};
