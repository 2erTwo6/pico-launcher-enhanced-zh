#pragma once
#include "gui/views/View.h"
#include "gui/materialDesign.h"
#include "core/math/Rgb.h"

class MaterialColorScheme;
class IVramManager;

class IconButtonView : public View
{
public:
    typedef void (*button_action_t)(IconButtonView* sender, void* arg);

    enum class Type
    {
        Standard,
        Filled,
        Tonal
    };

    enum class State
    {
        NoToggle,
        ToggleUnselected,
        ToggleSelected
    };

    void SetIconVramOffset(u32 vramOffset) { _iconVramOffset = vramOffset; }

    Rectangle GetBounds() const override
    {
        return Rectangle(_position, 32, 32);
    }

    void SetAction(button_action_t action, void* arg)
    {
        _action = action;
        _actionArg = arg;
    }

    /// @brief Optional second action fired by holding the button (A or pen)
    ///        for ~half a second. Setting it moves the short action to the
    ///        release edge, so a long press never also fires the short one.
    void SetLongAction(button_action_t longAction)
    {
        _longAction = longAction;
    }

    void SetState(State state)
    {
        _state = state;
    }

    /// @brief A disabled button is drawn with a faded icon and ignores presses.
    ///        Used for affordances that exist but cannot act right now, e.g. the
    ///        delete button while a folder is highlighted - dimming is honest,
    ///        whereas hiding it would shift every other button in the app bar
    ///        as the selection moves.
    void SetEnabled(bool enabled)
    {
        _enabled = enabled;
    }

    constexpr bool IsEnabled() const { return _enabled; }

    /// @brief Overrides the icon tint, e.g. to signal an active filter.
    void SetIconColorOverride(const Rgb<8, 8, 8>& color)
    {
        _iconColorOverride = color;
        _hasIconColorOverride = true;
    }

    void ClearIconColorOverride()
    {
        _hasIconColorOverride = false;
    }

    bool HandleInput(const InputProvider& inputProvider, FocusManager& focusManager) override;
    void HandlePenDown(const Point& touchPoint, FocusManager& focusManager) override;
    void HandlePenMove(const Point& touchPoint, FocusManager& focusManager) override;
    void HandlePenUp(const Point& lastTouchPoint, FocusManager& focusManager) override;

    void SetFocused(bool focused) override
    {
        // a pending A-hold is only valid while this button keeps focus:
        // losing focus mid-hold must not let a later refocus resume the count
        if (!focused)
            _heldFrames = 0;
        View::SetFocused(focused);
    }

protected:
    u32 _iconVramOffset;
    md::sys::color _backgroundColor;
    button_action_t _action;
    button_action_t _longAction = nullptr;
    void* _actionArg;
    Type _type;
    State _state;
    const MaterialColorScheme* _materialColorScheme;
    bool _penDown = false;
    /// @brief Frames the A button has been held, 0 when no press is pending.
    ///        Only counted when a long action is set. Separate from the pen
    ///        counter: HandleInput runs every focused frame and resets this,
    ///        which must not disturb an in-progress pen hold.
    int _heldFrames = 0;
    /// @brief Frames the pen has been held on the button, 0 when none.
    int _penHeldFrames = 0;
    Rgb<8, 8, 8> _iconColorOverride;
    bool _hasIconColorOverride = false;
    bool _enabled = true;

    /// @brief Icon tint: the override when set, the scheme role otherwise.
    Rgb<8, 8, 8> GetIconColor() const;
    Rgb<8, 8, 8> FadeIfDisabled(const Rgb<8, 8, 8>& color) const;

    IconButtonView(Type type, State state,
        md::sys::color backgroundColor, const MaterialColorScheme* materialColorScheme)
        : _iconVramOffset(0), _backgroundColor(backgroundColor)
        , _action(nullptr), _actionArg(nullptr), _type(type), _state(state)
        , _materialColorScheme(materialColorScheme) { }

    // One rule for every icon button, in the 2D and the 3D view alike, so that
    // what a button looks like always means the same thing:
    //  - the circle says SELECTED: a selected button sits on the container
    //    tone (secondaryContainer), an unselected one on its resting circle
    //    (Tonal) or on nothing at all (Standard);
    //  - FOCUS takes the icon to the theme's accent (primary) and lays a light
    //    veil of that accent over whatever circle the button has (Material 3's
    //    focus state layer); a Standard button with no circle of its own gets
    //    just the veil. The veil is faint on purpose: the circle underneath
    //    still says whether the button is selected.
    // Focus used to repaint the circle in the container tone instead, which
    // made a focused option and a selected one the same light circle.

    /// @brief Whether the button has a circle of its own: always for Tonal and
    ///        Filled, and for Standard only while it is selected.
    bool IsCircleBackgroundVisible() const;

    /// @brief The circle to draw this frame: the button's own circle, veiled
    ///        towards the accent while focused, or just the veil for a button
    ///        with no circle of its own. False when nothing is drawn.
    bool GetDrawnCircleColor(Rgb<8, 8, 8>& color) const;
    md::sys::color GetCircleBackgroundColor() const;
    md::sys::color GetForegroundColor() const;

    /// @brief Icon tint while focused (or pressed): the theme's accent. An icon
    ///        colour override still wins; none is set today. There is no Filled
    ///        button either: its selected circle IS the accent, so one would need
    ///        a focus look of its own.
    Rgb<8, 8, 8> GetFocusIconColor() const;

    /// @brief The icon tint for the current frame: focused or resting.
    Rgb<8, 8, 8> GetDrawnIconColor() const
    {
        return _isFocused || _penDown ? GetFocusIconColor() : GetIconColor();
    }
};