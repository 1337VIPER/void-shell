#pragma once

#include <map>
#include <string>
#include <vector>

#include "common.h"

// A folder or app kept as a bubble in the frame's margin.
enum class ShortcutKind { App, Folder };

struct Shortcut {
    ShortcutKind kind = ShortcutKind::App;
    std::wstring target;   // what opens it: a path, a shell parsing name, or shell:AppsFolder\<id>
    std::wstring name;
    std::wstring monitor;  // the monitor's device name (\\.\DISPLAY1); empty means the main one
    int band = 1;          // the frame bar it sits in: 0 top, 1 left, 2 right, 3 bottom
    float fx = 0.0f;       // where in that band, 0..1 across and down
    float fy = 0.0f;
};

// The bands a kind of shortcut lives in: apps down the sides, folders along the top and bottom.
inline bool BandAllows(ShortcutKind kind, int band) {
    return kind == ShortcutKind::App ? (band == 1 || band == 2) : (band == 0 || band == 3);
}

// User choices, kept in void-shell.ini next to the exe.
struct Settings {
    // The void
    UINT hotkeyModifiers = MOD_CONTROL | MOD_ALT;
    UINT hotkeyKey = VK_SPACE;
    bool pauseInFullscreen = true;  // the shortcut, frame and snapping step aside for fullscreen games and videos

    // Shell frame
    bool frame = true;
    bool frameAllMonitors = true;  // false: main monitor only
    bool frameWide = true;         // a broad margin around the desktop instead of a slim border
    int frameMargin = 1;           // wide margin size: 0 small, 1 medium, 2 large
    bool matrixRain = true;        // code raining through the wide margin
    int rainColor = 0;             // 0 Venom crimson, 1 green, 2 sheen blue
    std::wstring brandName = L"VenomMind";  // shown on the frame and in settings
    bool systemStats = true;       // CPU, GPU and memory orbs at the top of the frame
    std::vector<Shortcut> shortcuts;  // bubbles in the margin, in the order they were added
    bool showSockets = true;          // empty sockets show where bubbles can go (they always show while dragging)

    // Dock
    bool dock = true;                // a taskbar of our own in the frame's bottom margin
    bool dockAllDesktops = false;    // false: only the current desktop's windows
    bool hideWindowsTaskbar = false; // auto-hide Windows' taskbar while the dock is on
    int taskbarRestore = -1;         // the taskbar's auto-hide state before we changed it: -1 untouched, 0 off, 1 on
    bool dockSeeded = false;         // the pins were copied from the Windows taskbar once
    std::vector<std::wstring> dockPins;

    // Zones
    bool snap = true;        // dragging a window snaps it into a zone
    bool autoPlace = false;  // newly opened windows go into the first free zone
    int defaultLayout = 3;   // for desktops without their own; index into ZoneLayouts() (3: Main and side)
    std::map<std::wstring, int> desktopLayouts;  // desktop GUID string -> its own layout

    // Startup
    bool startAtLogin = false;
    bool runAsAdmin = false;  // lets snapping reach apps that run as administrator

    int LayoutFor(const GUID& desktop) const;
    void SetLayoutFor(const GUID& desktop, int layout);

    void Load(const std::wstring& path);
    void Save(const std::wstring& path) const;
};

// "Ctrl + Alt + Space" style text for a hotkey.
std::wstring HotkeyText(UINT modifiers, UINT key);
