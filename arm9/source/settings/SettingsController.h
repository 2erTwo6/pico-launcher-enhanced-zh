#pragma once
#include "fat/ff.h"
#include "ISettingsController.h"
#include "ThemeFolderDeleter.h"
#include "ThemeInfoManager.h"
#include "themes/ThemeRepository.h"

class IAppSettingsService;

class SettingsController : public ISettingsController
{
public:
    SettingsController(IAppSettingsService* appSettingsService, TaskQueueBase* ioTaskQueue);

    void Initialize() override;
    void NavigateUp() override;
    void SelectTheme(const char* themeFolderName) override;
    bool CanDeleteTheme(int themeIndex) const override;
    void RequestDeleteTheme(int themeIndex) override;
    void ConfirmDeleteTheme() override;
    void CancelDeleteTheme() override;
    void EndDeleteTheme() override;
    void UpdateDeleteTheme() override;
    int TakeReopenIndex() override;

    ThemeDeleteState GetDeleteState() const override { return _deleteState; }
    const char* GetDeleteFolderName() const override { return _deleteFolderName; }
    const char16_t* GetDeleteThemeName() const override { return _deleteThemeName; }
    const char* GetDeleteStatus() const override { return _deleteStatus; }

    ThemeInfoManager& GetThemeInfoManager() const override { return *_themeInfoManager; }
    const ThemeRepository& GetThemeRepository() const override { return _themeRepository; }
    TaskQueueBase* GetIoTaskQueue() const override { return _ioTaskQueue; }

private:
    IAppSettingsService* _appSettingsService;
    TaskQueueBase* _ioTaskQueue;
    ThemeRepository _themeRepository;
    std::unique_ptr<ThemeInfoManager> _themeInfoManager;

    ThemeDeleteState _deleteState = ThemeDeleteState::None;
    /// Full folder name, copied from ThemeRepository when the delete is asked for.
    TCHAR _deleteFolderName[FF_LFN_BUF + 1] = {};
    /// The name the list shows (ThemeInfo keeps 64 characters); empty when the
    /// folder has no readable theme.json.
    char16_t _deleteThemeName[65] = {};
    const char* _deleteStatus = nullptr;
    /// The theme setting when the delete was asked for, so the IO thread never
    /// reads the live settings.
    char _deleteActiveTheme[65] = {};
    /// Written by the IO task, read on the main thread once _deleteTaskDone is set.
    ThemeDeleteResult _deleteResult = ThemeDeleteResult::Ok;
    bool _deleteRemovedSomething = false;
    volatile bool _deleteTaskDone = false;
    int _deleteIndex = -1;
    /// Set when the list must be read again after the result has been shown.
    bool _restartAfterResult = false;

    void SetDeleteResultStatus();
    void RestartSelector();
};
