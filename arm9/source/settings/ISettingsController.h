#pragma once
class TaskQueueBase;

/// @brief Where a theme delete is. Only one runs at a time, and a new request is
///        taken only from None.
enum class ThemeDeleteState
{
    None,
    Confirming,
    Deleting,
    Finished
};

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
    virtual void ConfirmDeleteTheme() = 0;
    virtual void CancelDeleteTheme() = 0;
    /// @brief Back to None once the result of a finished delete has been shown.
    virtual void EndDeleteTheme() = 0;
    /// @brief Called every frame on the main thread; moves Deleting to Finished
    ///        once the IO thread is done.
    virtual void UpdateDeleteTheme() = 0;
    /// @brief The list position to open on after the selector restarted itself
    ///        because of a delete, once; -1 when it didn't.
    virtual int TakeReopenIndex() = 0;

    virtual ThemeDeleteState GetDeleteState() const = 0;
    /// @brief The folder and the name copied when the delete was requested.
    virtual const char* GetDeleteFolderName() const = 0;
    virtual const char16_t* GetDeleteThemeName() const = 0;
    /// @brief How the delete went, for the sheet; nullptr while there is nothing to say.
    virtual const char* GetDeleteStatus() const = 0;

    virtual ThemeInfoManager& GetThemeInfoManager() const = 0;
    virtual const ThemeRepository& GetThemeRepository() const = 0;
    virtual TaskQueueBase* GetIoTaskQueue() const = 0;
};
