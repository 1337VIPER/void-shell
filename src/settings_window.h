#pragma once

#include <functional>
#include <string>
#include <vector>

#include "canvas.h"
#include "common.h"
#include "settings.h"

// What the settings page shows besides the settings themselves.
struct SettingsContext {
    std::wstring desktopName = L"Desktop 1";  // the current desktop
    int desktopLayout = 3;                    // its zone layout
    bool elevated = false;
    int dockPins = 0;  // how many apps are pinned to the dock
    std::wstring startupNote;  // a startup problem to explain, if any
};

enum class SettingId {
    None,
    Close,
    Tab,
    Hotkey,
    PauseInFullscreen,
    Frame,
    FrameStyle,
    FrameMargin,
    Rain,
    RainColor,
    SystemStats,
    ShowSockets,
    FrameMonitors,
    Dock,
    DockDesktops,
    HideTaskbar,
    ResetPins,
    Snap,
    AutoPlace,
    DesktopLayout,
    DefaultLayout,
    ResetLayouts,
    StartAtLogin,
    RunAsAdmin,
    RestartElevated,
    OpenLog,
    OpenFolder,
};

constexpr int kSettingsTabs = 5;  // The void, Frame, Dock, Zones, Startup

// The settings page as layout and painting only, so it can be drawn and hit-tested without a window.
class SettingsView {
public:
    void Build(const Settings& settings, const SettingsContext& context, float scale, int tab);
    int Width() const { return width_; }
    int Height() const { return height_; }
    // The control under a point; `part` is the tab, segment or layout card for multi-part controls, else -1.
    SettingId HitTest(POINT point, int* part) const;
    bool InTitleBar(POINT point) const { return point.y < titleHeight_; }
    void Paint(Canvas& canvas, SettingId hover, int hoverPart, bool recording,
               const std::wstring& recordingText) const;

private:
    enum class Kind { Section, Toggle, Choice, Layouts, Hotkey, Button, Link, Note };
    struct Item {
        Kind kind;
        SettingId id;
        RECT rect;     // the whole row
        RECT control;  // the switch, field or button
        std::wstring label;
        std::wstring detail;
        bool on;
        bool enabled;
        int value;
        std::vector<std::wstring> options;
        std::vector<RECT> parts;  // segments or layout cards
    };

    // Lays out one tab's rows below the tab strip; returns the bottom of the last row.
    LONG BuildTab(const Settings& settings, const SettingsContext& context, int tab);
    void PaintItem(Canvas& canvas, const Item& item, SettingId hover, int hoverPart, bool recording,
                   const std::wstring& recordingText) const;

    std::vector<Item> items_;
    std::vector<RECT> tabs_;
    int tab_ = 0;
    std::wstring brand_;
    RECT close_ = {};
    int width_ = 0;
    int height_ = 0;
    int titleHeight_ = 0;
    int tabsBottom_ = 0;
    float scale_ = 1.0f;
};

// The settings window: a dark glass page in the Venom style. It only shows choices; the app applies them through
// the callbacks and then calls Update with the result.
class SettingsWindow {
public:
    struct Callbacks {
        std::function<void(SettingId id, int part)> changed;    // a toggle, choice, layout card or button
        std::function<bool(UINT modifiers, UINT key)> setHotkey;  // false if Windows refused the shortcut
        std::function<void(bool recording)> recording;           // pause the global shortcut while recording
    };

    bool Init(HINSTANCE instance, HICON icon, Callbacks callbacks);
    void Show(const Settings& settings, const SettingsContext& context);
    void ShowTab(int tab, const Settings& settings, const SettingsContext& context);
    void Update(const Settings& settings, const SettingsContext& context);
    bool IsOpen() const { return window_ != nullptr; }
    void Close();

private:
    static LRESULT CALLBACK Proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT Handle(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    void Rebuild();
    void Repaint();
    void Activate(SettingId id, int part);
    void OnKey(UINT key);
    void StopRecording();

    HINSTANCE instance_ = nullptr;
    Callbacks callbacks_;
    HWND window_ = nullptr;
    SettingsView view_;
    Canvas canvas_;
    Settings settings_;
    SettingsContext context_;
    float scale_ = 1.0f;
    int tab_ = 0;
    SettingId hover_ = SettingId::None;
    int hoverPart_ = -1;
    bool recording_ = false;
    std::wstring recordingText_;
};
