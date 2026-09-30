#include "common.h"
#include <libtwl/dma/dmaNitro.h>
#include <libtwl/gfx/gfx.h>
#include <libtwl/gfx/gfx3d.h>
#include <libtwl/gfx/gfxBackground.h>
#include <libtwl/gfx/gfxOam.h>
#include <libtwl/gfx/gfxPalette.h>
#include <libtwl/mem/memVram.h>
#include "core/math/RgbMixer.h"
#include "gui/GraphicsContext.h"
#include "gui/Gx.h"
#include "romBrowser/views/deleteconfirm/DeleteConfirmBottomSheetView.h"
#include "settings/viewModels/ThemeDeleteConfirmViewModel.h"
#include "settings/viewModels/ThemeListViewModel.h"
#include "themes/ThemeInfoFactory.h"
#include "themes/ThemeFactory.h"
#include "SettingsProcess.h"

/// How long the sheet shows how a delete went before it closes by itself.
#define DELETE_RESULT_FRAMES    120

SettingsProcess::SettingsProcess(IAppSettingsService& appSettingsService)
    : _mainObjPltt(GFX_PLTT_OBJ_MAIN)
    , _mainObjVram(GFX_OBJ_MAIN)
    , _mainObjDialogVram(GFX_OBJ_MAIN, 128 * 1024)
    , _subObjVram(GFX_OBJ_SUB)
    , _textureVram((vu16*)0x06860000)
    , _texturePaletteVram((vu16*)0x6880000)
    , _mainVramContext(nullptr, &_mainObjVram, &_textureVram, &_texturePaletteVram)
    , _subVramContext(nullptr, &_subObjVram, nullptr, nullptr)
    , _appSettingsService(appSettingsService)
    , _inputProvider(&_keyInputSource, &_touchInputSource)
    , _inputRepeater(&_inputProvider,
        InputKey::DpadLeft | InputKey::DpadRight | InputKey::DpadUp | InputKey::DpadDown | InputKey::L | InputKey::R,
        25, 8)
    , _dialogPresenter(&_focusManager, &_mainObjDialogVram) { }

void SettingsProcess::Run()
{
    InitVramMapping();
    gx_init();

    _chipViewVram = ChipView::UploadGraphics(_mainObjVram);
    _iconButtonViewVram = IconButton2DView::UploadGraphics(_mainObjVram);

    mem_setVramEMapping(MEM_VRAM_E_LCDC);
    _rgb6Palette.UploadGraphics(_mainVramContext);
    mem_setVramEMapping(MEM_VRAM_E_TEX_PLTT_SLOT_0123);

    LoadTheme();
    // also resets the scrim and the sheet layers the browser may have left set
    _dialogPresenter.InitVram();

    _settingsController = std::make_unique<SettingsController>(&_appSettingsService, &_ioTaskQueue);
    _settingsController->Initialize();

    auto viewModel = SharedPtr<ThemeListViewModel>::MakeShared(_settingsController.get(),
        _appSettingsService.GetAppSettings().theme);
    _themeListBottomView = ThemeListBottomView::CreateShared(viewModel,
        &_theme->GetMaterialColorScheme(), _theme->GetRomBrowserViewFactory(),
        _theme->GetThemeFileIconFactory(), &_vblankTextureLoader);
    _themeListBottomView->InitVram(_mainVramContext);
    _themeListBottomView->Focus(_focusManager);

    _themeListTopView = ThemeListTopView::CreateShared(viewModel, &_theme->GetMaterialColorScheme(), _theme->GetFontRepository());
    _themeListTopView->InitVram(_subVramContext);

    _ioTaskQueue.StartThread(1, _ioTaskThreadStack, sizeof(_ioTaskThreadStack));

    const auto& materialColorScheme = _theme->GetMaterialColorScheme();

    auto scrimBlendColor = Rgb<8, 8, 8>(
        materialColorScheme.inverseOnSurface.r + (materialColorScheme.scrim.r - materialColorScheme.inverseOnSurface.r) * 5 / 16,
        materialColorScheme.inverseOnSurface.g + (materialColorScheme.scrim.g - materialColorScheme.inverseOnSurface.g) * 5 / 16,
        materialColorScheme.inverseOnSurface.b + (materialColorScheme.scrim.b - materialColorScheme.inverseOnSurface.b) * 5 / 16);

    RgbMixer::MakeGradientPalette((u16*)GFX_PLTT_BG_MAIN, scrimBlendColor, materialColorScheme.GetColor(md::sys::color::surfaceContainerLow));

    GFX_PLTT_BG_MAIN[0] = ColorConverter::ToGBGR565(materialColorScheme.inverseOnSurface);
    GFX_PLTT_BG_MAIN[31] = ColorConverter::ToGBGR565(materialColorScheme.scrim);
    // BG1 and BG2 are the sheet and its scrim, as in App
    REG_DISPCNT = 0x211F1B;
    REG_BG0HOFS = 0;
    REG_BG0VOFS = 0;
    REG_BG0CNT = 3;
    REG_DISPCNT_SUB = 0x40211015;
    GFX_PLTT_BG_SUB[0] = ColorConverter::ToGBGR565(materialColorScheme.inverseOnSurface);

    REG_DMA3FILL = 0;
    dma_ntrSetParams(3, (const void*)&REG_DMA3FILL, GFX_BG_SUB,
        DMACNT_ENABLE | DMACNT_MODE_IMMEDIATE | DMACNT_32BIT |
        DMACNT_SRC_MODE_FIXED | DMACNT_DST_MODE_INCREMENT |
        DMACNT_COUNT((256 * 192 * 2) >> 2));
    dma_ntrWait(3);

    Gx::MtxMode(GX_MTX_MODE_PROJECTION);
    mtx43_t orthoMtx =
    {
        2048, 0, 0,
        0, -21845, 0,
        0, 0, 4096 >> 5,
        -4096, 4096, 0
    };
    Gx::MtxLoad43(&orthoMtx);

    _vcountIrqStarted = false;
    rtos_disableIrqMask(RTOS_IRQ_VCOUNT);
    rtos_setIrqFunc(RTOS_IRQ_VCOUNT, [] (u32 mask) { ((SettingsProcess*)gProcessManager.GetRunningProcess())->VCountIrq(); });

    _fadeAnimator = Animator(16, 0, 16, &md::sys::motion::easing::linear);

    MainLoop();

    rtos_disableIrqMask(RTOS_IRQ_VCOUNT);
    rtos_setIrqFunc(RTOS_IRQ_VCOUNT, nullptr);

    // Let the IO thread finish its tasks while the controller they use is still
    // alive; the members are destroyed controller first, queue last.
    _ioTaskQueue.StopThread();
}

void SettingsProcess::Exit()
{
    _fadeAnimator.Goto(16, 16, &md::sys::motion::easing::linear);
    _exit = true;
}

void SettingsProcess::InitVramMapping() const
{
    mem_setVramAMapping(MEM_VRAM_AB_TEX_SLOT_1);
    mem_setVramBMapping(MEM_VRAM_AB_MAIN_OBJ_00000);
    mem_setVramCMapping(MEM_VRAM_C_SUB_BG_00000);
    mem_setVramDMapping(MEM_VRAM_D_TEX_SLOT_0);
    mem_setVramEMapping(MEM_VRAM_E_TEX_PLTT_SLOT_0123);
    mem_setVramFMapping(MEM_VRAM_FG_MAIN_BG_00000);
    mem_setVramGMapping(MEM_VRAM_FG_MAIN_BG_04000);
    mem_setVramHMapping(MEM_VRAM_H_SUB_BG_EXT_PLTT_SLOT_0123);
    mem_setVramIMapping(MEM_VRAM_I_SUB_OBJ_00000);
}

void SettingsProcess::LoadTheme()
{
    // The selector is always drawn as a Material theme. A custom theme's own
    // cells and backgrounds could make the one screen where you change it hard
    // to read. It keeps the active theme's primary color and dark setting, so it
    // still looks like it, and it has no folder, so it loads no files of its own.
    ThemeInfoFactory themeInfoFactory;
    auto activeThemeInfo = themeInfoFactory.CreateFromThemeFolder(_appSettingsService.GetAppSettings().theme);
    std::unique_ptr<ThemeInfo> themeInfo;
    if (activeThemeInfo)
    {
        themeInfo = std::make_unique<ThemeInfo>("", ThemeType::Material, "", "", "",
            activeThemeInfo->GetPrimaryColor(), activeThemeInfo->GetIsDarkTheme());
    }
    else
    {
        LOG_DEBUG("Failed to load theme '%s'. Using fallback theme.\n", _appSettingsService.GetAppSettings().theme.GetString());
        themeInfo = themeInfoFactory.CreateFallbackTheme();
    }
    activeThemeInfo.reset();
    _theme = ThemeFactory().CreateFromThemeInfo(themeInfo.get());
    themeInfo.reset();
    _theme->LoadRomBrowserResources(_mainVramContext, _subVramContext);
    // _topBackground = _theme->CreateRomBrowserTopBackground();
    // _topBackground->LoadResources(*_theme, _subVramContext);
    _bottomBackground = _theme->CreateRomBrowserBottomBackground();
    _bottomBackground->LoadResources(*_theme, _mainVramContext);
}

void SettingsProcess::MainLoop()
{
    bool fadeIn = true;
    while (true)
    {
        Update();
        Draw();
        VBlank::Wait();
        VBlank();
        if (_exit)
        {
            bool fadeComplete = _fadeAnimator.Update();
            REG_MASTER_BRIGHT = 0x4000 | _fadeAnimator.GetValue();
            REG_MASTER_BRIGHT_SUB = 0x4000 | _fadeAnimator.GetValue();
            if (fadeComplete)
            {
                break;
            }
        }
        else if (fadeIn)
        {
            bool fadeComplete = _fadeAnimator.Update();
            if (fadeComplete)
            {
                fadeIn = false;
                REG_BLDCNT_SUB = 0;
                REG_MASTER_BRIGHT = 0;
                REG_MASTER_BRIGHT_SUB = 0;
            }
            else
            {
                int fade = _fadeAnimator.GetValue();
                REG_MASTER_BRIGHT = 0x4000 | fade;
                REG_MASTER_BRIGHT_SUB = 0x4000 | fade;
            }
        }
    }
}

void SettingsProcess::Update()
{
    if (!_exit)
    {
        HandleInput();
    }
    SyncDeleteSheet();

    // if (_topBackground)
    // {
    //     _topBackground->Update();
    // }
    if (_bottomBackground)
    {
        _bottomBackground->Update();
    }

    _dialogPresenter.Update();

    _themeListBottomView->Update();
    _themeListTopView->Update();
}

void SettingsProcess::HandleInput()
{
    _focusManager.Update(_inputRepeater);
    // While a delete is asked for, running or showing its result, taps belong
    // to the sheet; the list behind it must not take them, even in the frame
    // before the sheet appears.
    bool toSheet = _dialogPresenter.IsBottomSheetVisible()
        || _settingsController->GetDeleteState() != ThemeDeleteState::None;
    Point touchPoint;
    if (_inputRepeater.Triggered(InputKey::Touch) &&
        _inputRepeater.GetCurrentTouchPoint(touchPoint))
    {
        // pen down
        if (toSheet)
            _dialogPresenter.HandlePenDown(touchPoint, _focusManager);
        else
            _themeListBottomView->HandlePenDown(touchPoint, _focusManager);
        _lastTouchPoint = touchPoint;
    }
    else if (_inputRepeater.Released(InputKey::Touch))
    {
        // pen up
        if (toSheet)
            _dialogPresenter.HandlePenUp(_lastTouchPoint, _focusManager);
        else
            _themeListBottomView->HandlePenUp(_lastTouchPoint, _focusManager);
    }
    else if (_inputRepeater.Current(InputKey::Touch)
        && _inputRepeater.GetCurrentTouchPoint(touchPoint))
    {
        // pen move
        if (toSheet)
            _dialogPresenter.HandlePenMove(touchPoint, _focusManager);
        else
            _themeListBottomView->HandlePenMove(touchPoint, _focusManager);
        _lastTouchPoint = touchPoint;
    }
}

void SettingsProcess::SyncDeleteSheet()
{
    switch (_settingsController->GetDeleteState())
    {
        case ThemeDeleteState::Confirming:
        {
            if (!_deleteSheetShown)
            {
                auto viewModel = SharedPtr<ThemeDeleteConfirmViewModel>::MakeShared(_settingsController.get());
                _dialogPresenter.ShowDialog(DeleteConfirmBottomSheetView::CreateShared(
                    std::move(viewModel), &_theme->GetMaterialColorScheme(), _theme->GetFontRepository()));
                _deleteSheetShown = true;
            }
            break;
        }
        case ThemeDeleteState::Finished:
        {
            // the sheet shows how it went for a moment, then closes by itself
            // once: a partial delete restarts the selector here, and the sheet
            // must keep its result on screen while the selector fades out
            if (++_deleteResultFrames == DELETE_RESULT_FRAMES)
            {
                _settingsController->EndDeleteTheme();
            }
            break;
        }
        case ThemeDeleteState::None:
        {
            // Forget the sheet at once: mashing A cancels it and asks again
            // while it is still sliding out, and that new request must get a
            // sheet of its own (the presenter queues it behind the old one).
            _deleteResultFrames = 0;
            _deleteSheetShown = false;
            // The delete sheet is the only one this screen has, so with no
            // delete asked for, any sheet still up is closed. CloseDialog does
            // nothing until a sheet is fully up, so it is asked every frame.
            if (_dialogPresenter.IsBottomSheetVisible())
            {
                _dialogPresenter.CloseDialog();
            }
            break;
        }
        case ThemeDeleteState::Deleting:
        {
            _settingsController->UpdateDeleteTheme();
            break;
        }
    }
}

void SettingsProcess::Draw()
{
    gx_reset();
    Gx::Viewport(0, 0, 255, 191);
    Gx::MtxMode(GX_MTX_MODE_POSITION_VECTOR);
    Gx::MtxIdentity();

    GraphicsContext mainGraphicsContext
    {
        &_mainOam,
        &_mainObjPltt,
        &_rgb6Palette
    };
    GraphicsContext subGraphicsContext
    {
        &_subOam,
        &_subObjPltt,
        nullptr
    };

    _mainOam.Clear();
    _subOam.Clear();
    _mainObjPltt.Reset();
    _subObjPltt.Reset();
    mainGraphicsContext.SetPriority(3);
    subGraphicsContext.SetPriority(2);

    // if (_topBackground)
    // {
    //     _topBackground->Draw(subGraphicsContext);
    // }
    if (_bottomBackground)
    {
        _bottomBackground->Draw(mainGraphicsContext);
    }

    _dialogPresenter.ApplyClipArea(mainGraphicsContext);
    _themeListBottomView->Draw(mainGraphicsContext);
    mainGraphicsContext.ResetClipArea();
    _dialogPresenter.Draw(mainGraphicsContext);
    _themeListTopView->Draw(subGraphicsContext);

    _mainObjPltt.EndOfFrame();

    Gx::SwapBuffers(GX_XLU_SORT_MANUAL, GX_DEPTH_MODE_Z);
}

void SettingsProcess::VBlank()
{
    dma_ntrStopDirect(0); // stop hblank dma
    _inputProvider.Sample();
    _inputRepeater.Update();
    _mainOam.Apply(GFX_OAM_MAIN);
    _subOam.Apply(GFX_OAM_SUB);
    _subObjPltt.Apply(GFX_PLTT_OBJ_SUB);

    if (!_vcountIrqStarted)
    {
        rtos_ackIrqMask(RTOS_IRQ_VCOUNT);
        rtos_enableIrqMask(RTOS_IRQ_VCOUNT);
        _vcountIrqStarted = true;
    }
    _mainObjPltt.VBlank();

    // if (_topBackground)
    // {
    //     _topBackground->VBlank();
    // }
    if (_bottomBackground)
    {
        _bottomBackground->VBlank();
    }

    _themeListBottomView->VBlank();
    _dialogPresenter.VBlank();
    _vblankTextureLoader.VBlank();

    _themeListTopView->VBlank();
}

void SettingsProcess::VCountIrq()
{
    _mainObjPltt.VCount();
}
