#pragma once
class TaskQueueBase;
class ThemeInfoManager;
class ThemeRepository;

class ISettingsController
{
public:
    virtual ~ISettingsController() = default;

    virtual void Initialize() = 0;
    virtual void NavigateUp() = 0;
    virtual void SelectTheme(const char* themeFolderName) = 0;

    /// @brief Whether the theme at this list position may be deleted: not the
    ///        theme in use, not one of the themes that come with the launcher, and
    ///        a folder name that can only mean that one folder. Names only, it does
    ///        not touch the card.
    virtual bool CanDeleteTheme(int themeIndex) const = 0;
    virtual void RequestDeleteTheme(int themeIndex) = 0;

    virtual ThemeInfoManager& GetThemeInfoManager() const = 0;
    virtual const ThemeRepository& GetThemeRepository() const = 0;
    virtual TaskQueueBase* GetIoTaskQueue() const = 0;
};
