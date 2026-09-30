#pragma once
#include "core/String.h"
#include "core/mini-printf.h"
#include "../IRomBrowserController.h"
#include "IDeleteConfirmViewModel.h"

/// @brief View model for the delete confirmation sheet. Copies the names at
///        construction time so nothing dangles while the sheet animates.
class DeleteConfirmViewModel : public IDeleteConfirmViewModel
{
public:
    explicit DeleteConfirmViewModel(IRomBrowserController* romBrowserController)
        : _romBrowserController(romBrowserController)
        , _fileName(romBrowserController->GetDeleteRomFileName())
    {
        const char* saveFileName = romBrowserController->GetDeleteSaveFileName();
        if (saveFileName && saveFileName[0] != 0)
            mini_snprintf(_detailLine, sizeof(_detailLine), "The save %s is also deleted", saveFileName);
        else
            _detailLine[0] = 0;
    }

    const char16_t* GetTitle() const override { return u"Delete game?"; }
    const char* GetNameLine() const override { return _fileName.GetString(); }
    const char* GetDetailLine() const override { return _detailLine; }

    void Confirm() override
    {
        _romBrowserController->ConfirmDelete();
    }

    void Cancel() override
    {
        _romBrowserController->CancelDelete();
    }

private:
    IRomBrowserController* _romBrowserController;
    String<char, 256> _fileName;
    char _detailLine[280];
};
