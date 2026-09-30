#include "common.h"
#include "backIcon.h"
#include "trashIcon.h"
#include "gui/GraphicsContext.h"
#include "gui/VramContext.h"
#include "gui/IVramManager.h"
#include "romBrowser/Theme/IRomBrowserViewFactory.h"
#include "SettingsAppBarView.h"

SettingsAppBarView::SettingsAppBarView(SharedPtr<ThemeListViewModel> viewModel, const IRomBrowserViewFactory* romBrowserViewFactory)
    : _viewModel(std::move(viewModel))
{
    _appBarView = romBrowserViewFactory->CreateAppBarView(0, 0, AppBarView::Orientation::Vertical, 1, 1);
    AddChildTail(_appBarView.GetPointer());

    _appBarView->SetButtonAction(APP_BAR_BUTTON_BACK, [] (IconButtonView* sender, void* arg)
    {
        ((ThemeListViewModel*)arg)->NavigateUp();
    }, _viewModel.GetPointer());

    _appBarView->SetButtonAction(APP_BAR_BUTTON_DELETE, [] (IconButtonView* sender, void* arg)
    {
        ((ThemeListViewModel*)arg)->RequestDeleteSelected();
    }, _viewModel.GetPointer());
}

void SettingsAppBarView::InitVram(const VramContext& vramContext)
{
    ViewContainer::InitVram(vramContext);

    const auto objVramManager = vramContext.GetObjVramManager();
    if (objVramManager)
    {
        u32 backIconVramOffset = objVramManager->Alloc(backIconTilesLen);
        dma_ntrCopy32(3, backIconTiles, objVramManager->GetVramAddress(backIconVramOffset), backIconTilesLen);
        _appBarView->SetButtonIcon(APP_BAR_BUTTON_BACK, backIconVramOffset);

        u32 trashIconVramOffset = objVramManager->Alloc(trashIconTilesLen);
        dma_ntrCopy32(3, trashIconTiles, objVramManager->GetVramAddress(trashIconVramOffset), trashIconTilesLen);
        _appBarView->SetButtonIcon(APP_BAR_BUTTON_DELETE, trashIconVramOffset);
    }
}

SharedPtr<View> SettingsAppBarView::MoveFocus(const SharedPtr<View>& currentFocus, FocusMoveDirection direction, View* source)
{
    if (!currentFocus)
    {
        return nullptr;
    }
    if (source == _appBarView.GetPointer())
    {
        return View::MoveFocus(currentFocus, direction, source);
    }
    else if (source == GetParent())
    {
        return _appBarView->MoveFocus(currentFocus, direction, this);
    }
    return nullptr;
}
