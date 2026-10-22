// Void Shell.
// Press Ctrl+Alt+Space: the desktop is captured, covered by an identical full-screen frame, and that frame curls
// into a ball while the camera pulls back into a starfield. Every Windows virtual desktop is a world there; click
// one to fly into it, or click the plus world to make a new desktop and fly into that.
// Around the desktop sits a glowing shell frame, and dragged windows snap into zones inside it. Each desktop can
// have its own zone layout; everything steps aside for fullscreen games; settings live in a window of their own.

#include "common.h"

#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

#include <wtsapi32.h>

#include "brand.h"
#include "capture.h"
#include "desktops.h"
#include "dock.h"
#include "frame.h"
#include "pickers.h"
#include "renderer.h"
#include "scene.h"
#include "selftest.h"
#include "settings.h"
#include "settings_window.h"
#include "startup.h"
#include "sysinfo.h"
#include "taskbar.h"
#include "wallpaper.h"
#include "zones.h"

namespace {

constexpr wchar_t kWindowClass[] = L"VoidShellOverlay";
constexpr wchar_t kAppName[] = L"VenomMind";
constexpr int kHotkeyId = 1;
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kOpenVoidMessage = WM_APP + 2;    // posted by the frame's orb
constexpr UINT kLayoutMenuMessage = WM_APP + 3;  // posted by the frame's layout button; lParam = screen point
constexpr UINT kOpenSettingsMessage = WM_APP + 4;  // posted by the frame's gear
constexpr UINT kDockSettingsMessage = WM_APP + 5;  // posted by the dock's menu
constexpr UINT kSwitchDesktopMessage = WM_APP + 6;  // posted by the dock; wParam = desktop index
constexpr UINT kDockSlotMessage = WM_APP + 7;     // the frame moved: the dock follows
constexpr UINT kStatsMessage = WM_APP + 8;        // the system monitor has a new reading
constexpr UINT kOpenStatsMessage = WM_APP + 9;    // the frame's orbs were clicked
constexpr UINT kAddShortcutMessage = WM_APP + 10;   // the frame's + button; lParam = screen point
constexpr UINT kOpenShortcutMessage = WM_APP + 11;  // a bubble was clicked; wParam = its index
constexpr UINT kShortcutMenuMessage = WM_APP + 12;  // a bubble was right-clicked; wParam = index, lParam = point
constexpr UINT kLaunchShortcutMessage = WM_APP + 13;  // a bubble was middle-clicked; wParam = its index
constexpr UINT kShortcutOpen = 1;
constexpr UINT kShortcutLocation = 2;
constexpr UINT kShortcutRemove = 3;
constexpr UINT kShortcutNew = 4;
constexpr UINT kShortcutClose = 5;
constexpr UINT_PTR kBubbleStateTimer = 5;      // which app bubbles' apps are running, every 1.5 s
constexpr UINT_PTR kBubbleStateSoonTimer = 6;  // the same, once, just after a click
constexpr UINT_PTR kBubblePreviewTimer = 7;    // hover previews over app bubbles
constexpr UINT_PTR kBubblePreviewRefresh = 8;  // a window was closed from a bubble's preview
constexpr ULONGLONG kBubblePreviewDelay = 450;
constexpr ULONGLONG kBubblePreviewHide = 350;
constexpr ULONGLONG kBubbleAfterClick = 900;
constexpr UINT kMenuOpen = 100;
constexpr UINT kMenuQuit = 101;
constexpr UINT kMenuFrame = 102;
constexpr UINT kMenuSnap = 103;
constexpr UINT kMenuAutoPlace = 104;
constexpr UINT kMenuSettings = 105;
constexpr UINT kMenuLayoutBase = 200;
constexpr UINT_PTR kReflowTimer = 1;
constexpr UINT_PTR kDesktopNameTimer = 2;
constexpr UINT_PTR kPictureTimer = 3;          // a moment after a desktop switch we didn't make
constexpr UINT_PTR kPeriodicPictureTimer = 4;  // keeps the current desktop's ball picture fresh
constexpr UINT kPeriodicPictureMs = 60000;

constexpr float kSettleSeconds = 0.15f;         // after a desktop switch, before grabbing its picture
constexpr float kSwitchTimeoutSeconds = 1.5f;   // give up waiting and enter with the old picture
constexpr float kBirthDelaySeconds = 0.55f;     // let a new world pop out of the plus before flying in

struct App {
    HWND window = nullptr;
    HICON icon = nullptr;
    UINT taskbarCreatedMessage = 0;
    Renderer renderer;
    VoidScene scene;
    VirtualDesktops desktops;
    bool graphicsReady = false;
    bool overlayVisible = false;
    HMONITOR monitor = nullptr;
    UINT width = 0;
    UINT height = 0;
    LARGE_INTEGER frequency = {};
    LARGE_INTEGER lastTick = {};

    std::vector<DesktopInfo> desktopList;
    GUID currentDesktop = {};
    std::vector<GUID> placeholderQueue;
    std::map<std::wstring, std::vector<uint8_t>> wallpaperCache;

    // Entering another desktop: switch under the overlay, then grab a real picture of it through the overlay.
    bool switching = false;
    bool switchSeen = false;
    bool switchCaptured = false;
    GUID switchTarget = {};
    float switchClock = 0.0f;
    float switchSeenAt = 0.0f;

    // A new world popping out of the plus world, entered once it has appeared.
    float birthTimer = -1.0f;
    GUID bornDesktop = {};

    // The shell frame, dock, zones and settings.
    ShellFrame frame;
    Dock dock;
    bool dockSlotPending = false;
    SystemMonitor systemMonitor;  // feeds the frame's CPU, GPU and memory orbs
    AppPicker appPicker;          // "Add an app" for the margin's bubbles
    POINT addPoint = {};          // where the + was clicked: new bubbles go on that monitor
    // App bubbles behave like taskbar icons: their windows are found as the dock finds them.
    DockModel bubbleModel;
    std::vector<std::wstring> bubblePins;  // the app bubbles' targets, as last given to bubbleModel
    std::vector<int> bubbleIndices;        // bubbleModel's apps -> shortcut index
    WindowPreview bubblePreview;
    int bubbleHover = -1;
    ULONGLONG bubbleHoverSince = 0;
    int bubbleChecked = -1;  // the hovered bubble whose windows were already looked up (none: no preview)
    int bubblePreviewFor = -1;
    ULONGLONG bubbleAwaySince = 0;
    ULONGLONG bubbleBlockedUntil = 0;
    ZoneSnapper snapper;
    Settings settings;
    std::wstring settingsPath;
    SettingsWindow settingsWindow;
    GUID activeDesktop = {};  // the desktop on screen, as last polled
    std::wstring activeDesktopName = L"Desktop 1";
    int nameTicks = 0;
    std::wstring startupNote;
    bool elevated = false;
    bool hotkeyRegistered = false;
    bool recordingHotkey = false;
    bool adminHintShown = false;

    // A fullscreen game or video is in front: the shortcut, the frame on its monitor and snapping are paused.
    bool gameMode = false;
    HMONITOR gameMonitor = nullptr;

    // While the void is open, the other monitors dim.
    std::vector<HWND> veils;
    BYTE veilAlpha = 0;

    // The frame's rain pauses while the session is locked or the PC runs on battery.
    bool locked = false;
};

App g_app;

void HideBubblePreview();

// Something is in flight that clicks, keys and focus changes must not interrupt.
bool Busy() { return g_app.switching || g_app.birthTimer >= 0.0f; }

std::wstring ExeDirectory() {
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring dir(path);
    const size_t slash = dir.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"" : dir.substr(0, slash + 1);
}


void AddTrayIcon() {
    NOTIFYICONDATAW data = {sizeof(data)};
    data.hWnd = g_app.window;
    data.uID = 1;
    data.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    data.uCallbackMessage = kTrayMessage;
    data.hIcon = g_app.icon;
    wcscpy_s(data.szTip, L"VenomMind");
    Shell_NotifyIconW(NIM_ADD, &data);
}

void RemoveTrayIcon() {
    NOTIFYICONDATAW data = {sizeof(data)};
    data.hWnd = g_app.window;
    data.uID = 1;
    Shell_NotifyIconW(NIM_DELETE, &data);
}

void SaveSettings() { g_app.settings.Save(g_app.settingsPath); }

int CurrentLayout() { return g_app.settings.LayoutFor(g_app.activeDesktop); }

// Zones follow the layout of the desktop a window lives on.
int LayoutForWindow(HWND window) {
    GUID desktop = {};
    if (!g_app.desktops.WindowDesktop(window, desktop) || desktop == GUID{}) desktop = g_app.activeDesktop;
    return g_app.settings.LayoutFor(desktop);
}

// The layouts for the desktop on screen.
HMENU BuildLayoutMenu() {
    HMENU menu = CreatePopupMenu();
    const auto& layouts = ZoneLayouts();
    for (size_t i = 0; i < layouts.size(); ++i) AppendMenuW(menu, MF_STRING, kMenuLayoutBase + UINT(i), layouts[i].name);
    CheckMenuRadioItem(menu, kMenuLayoutBase, kMenuLayoutBase + UINT(layouts.size()) - 1,
                       kMenuLayoutBase + UINT(CurrentLayout()), MF_BYCOMMAND);
    return menu;
}

void TrackMenu(HMENU menu, POINT at, UINT flags) {
    SetForegroundWindow(g_app.window);  // so the menu closes when you click elsewhere
    TrackPopupMenu(menu, flags | TPM_RIGHTBUTTON, at.x, at.y, 0, g_app.window, nullptr);
    PostMessageW(g_app.window, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

void ShowTrayMenu() {
    const Settings& s = g_app.settings;
    const std::wstring open = L"Open the void\t" + HotkeyText(s.hotkeyModifiers, s.hotkeyKey);
    const std::wstring layout = L"Zone layout for " + g_app.activeDesktopName;
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kMenuOpen, open.c_str());
    AppendMenuW(menu, MF_STRING, kMenuSettings, L"Settings…");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (s.frame ? MF_CHECKED : 0u), kMenuFrame, L"Shell frame");
    AppendMenuW(menu, MF_STRING | (s.snap ? MF_CHECKED : 0u), kMenuSnap, L"Snap windows to zones");
    AppendMenuW(menu, MF_STRING | (s.autoPlace ? MF_CHECKED : 0u), kMenuAutoPlace, L"Place new windows in zones");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(BuildLayoutMenu()), layout.c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuQuit, L"Quit Void Shell");
    SetMenuDefaultItem(menu, kMenuOpen, FALSE);
    POINT cursor;
    GetCursorPos(&cursor);
    TrackMenu(menu, cursor, TPM_BOTTOMALIGN);
}

SettingsContext MakeSettingsContext() {
    SettingsContext context;
    context.desktopName = g_app.activeDesktopName;
    context.desktopLayout = CurrentLayout();
    context.elevated = g_app.elevated;
    context.dockPins = int(g_app.settings.dockPins.size());
    context.startupNote = g_app.startupNote;
    return context;
}

void RefreshSettingsWindow() { g_app.settingsWindow.Update(g_app.settings, MakeSettingsContext()); }

void UpdateTrayTip() {
    NOTIFYICONDATAW data = {sizeof(data)};
    data.hWnd = g_app.window;
    data.uID = 1;
    data.uFlags = NIF_TIP;
    const std::wstring tip =
        g_app.settings.brandName + L" (" + HotkeyText(g_app.settings.hotkeyModifiers, g_app.settings.hotkeyKey) + L")";
    wcsncpy_s(data.szTip, tip.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &data);
}

// The desktop on screen: its name goes on the frame's top bar and its layout drives the zones. Desktops can be
// switched without us (Task View, Win+Ctrl+arrows), so this is polled; names are re-read every few seconds in case
// one was renamed.
void UpdateCurrentDesktop(bool force = false) {
    GUID current = {};
    if (!g_app.desktops.Current(current)) return;
    if (!force && current == g_app.activeDesktop && ++g_app.nameTicks < 5) return;
    const bool switched = current != g_app.activeDesktop;
    g_app.nameTicks = 0;
    g_app.activeDesktop = current;
    std::vector<DesktopInfo> desktops;
    GUID ignored = {};
    std::wstring name = L"Desktop";
    if (g_app.desktops.List(desktops, ignored)) {
        for (size_t i = 0; i < desktops.size(); ++i) {
            if (desktops[i].id == current)
                name = desktops[i].name.empty() ? L"Desktop " + std::to_wstring(i + 1) : desktops[i].name;
        }
    }
    g_app.activeDesktopName = name;
    g_app.frame.SetDesktopName(name);
    std::vector<std::wstring> names;
    int index = 0;
    for (size_t i = 0; i < desktops.size(); ++i) {
        names.push_back(desktops[i].name.empty() ? L"Desktop " + std::to_wstring(i + 1) : desktops[i].name);
        if (desktops[i].id == current) index = int(i);
    }
    g_app.dock.SetDesktops(names, index);
    g_app.frame.SetLayout(CurrentLayout());
    if (switched || force) RefreshSettingsWindow();
    // Switched outside the void (Task View, Win+Ctrl+arrows): refresh this desktop's ball picture once it settles.
    if (switched && !g_app.overlayVisible) SetTimer(g_app.window, kPictureTimer, 700, nullptr);
}

// The global shortcut, unless a game is in front or the settings window is recording a new one.
void ApplyHotkey() {
    UnregisterHotKey(g_app.window, kHotkeyId);
    g_app.hotkeyRegistered = false;
    if (g_app.gameMode || g_app.recordingHotkey) return;
    const Settings& s = g_app.settings;
    g_app.hotkeyRegistered = RegisterHotKey(g_app.window, kHotkeyId, s.hotkeyModifiers | MOD_NOREPEAT, s.hotkeyKey);
    if (!g_app.hotkeyRegistered)
        Log("%ls is already registered by another app", HotkeyText(s.hotkeyModifiers, s.hotkeyKey).c_str());
}

void ApplySnapping() {
    const Settings& s = g_app.settings;
    g_app.snapper.Configure(s.snap && !g_app.gameMode, s.autoPlace && !g_app.gameMode);
}

void ApplyFrameStyle() {
    const Settings& s = g_app.settings;
    FrameStyle style;
    style.wide = s.frameWide;
    style.margin = s.frameMargin;
    style.rain = s.matrixRain;
    style.rainColor = RainColor(std::clamp(s.rainColor, 0, 2));
    style.dockBand = s.dock ? DockBandHeight(1.0f) : 0;
    g_app.frame.SetBrand(s.brandName);
    g_app.frame.SetStatsEnabled(s.systemStats);
    g_app.frame.SetShowSockets(s.showSockets);
    g_app.frame.SetStyle(style);
}

bool OnBattery() {
    SYSTEM_POWER_STATUS power = {};
    return GetSystemPowerStatus(&power) && power.ACLineStatus == 0;
}

// The rain is the only thing that runs continuously, so it stops whenever nobody can enjoy it.
void UpdateRainAllowed() {
    g_app.frame.SetAnimationAllowed(!g_app.locked && !OnBattery() && !g_app.overlayVisible);
    // The orbs read the machine once a second on their own thread, only while they can be seen.
    const Settings& s = g_app.settings;
    g_app.systemMonitor.SetActive(s.frame && s.systemStats && !g_app.locked && !g_app.overlayVisible, g_app.window,
                            kStatsMessage);
}

// The dock sits in the main monitor's bottom band, so it moves, hides and shows with the frame.
void UpdateDockSlot() {
    g_app.dockSlotPending = false;
    RECT slot;
    float scale = 1.0f;
    if (g_app.frame.DockSlot(slot, scale)) {
        g_app.dock.SetSlot(&slot, scale);
    } else {
        g_app.dock.SetSlot(nullptr, 1.0f);
    }
}

// Windows' taskbar auto-hides while the dock replaces it, if asked. Its own setting is remembered first and put back
// when the option or the dock goes off, and when Void Shell quits.
void RestoreTaskbar() {
    Settings& s = g_app.settings;
    if (s.taskbarRestore < 0) return;
    if (TaskbarAutoHide() != (s.taskbarRestore == 1)) SetTaskbarAutoHide(s.taskbarRestore == 1);
    s.taskbarRestore = -1;
    SaveSettings();
}

void ApplyTaskbar() {
    Settings& s = g_app.settings;
    if (!(s.frame && s.dock && s.hideWindowsTaskbar)) {
        RestoreTaskbar();
        return;
    }
    if (s.taskbarRestore < 0) {
        s.taskbarRestore = TaskbarAutoHide() ? 1 : 0;
        SaveSettings();
    }
    if (!TaskbarAutoHide()) SetTaskbarAutoHide(true);
}

void ApplyDock() {
    const Settings& s = g_app.settings;
    g_app.dock.SetAllDesktops(s.dockAllDesktops);
    g_app.dock.SetEnabled(s.frame && s.dock);
    UpdateDockSlot();
    ApplyTaskbar();
}

void ApplySettings() {
    const Settings& s = g_app.settings;
    g_app.frame.SetAllMonitors(s.frameAllMonitors);
    ApplyFrameStyle();
    g_app.frame.SetEnabled(s.frame);
    ApplyDock();
    UpdateRainAllowed();
    ApplySnapping();
    ApplyHotkey();
    UpdateCurrentDesktop(true);
}

void ChooseLayout(int index) {
    g_app.settings.SetLayoutFor(g_app.activeDesktop, index);
    g_app.frame.SetLayout(index);
    g_app.snapper.Reflow();
    SaveSettings();
    RefreshSettingsWindow();
}

bool InitGraphics() {
    g_app.renderer.Reset();
    const HMONITOR primary = MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
    const ComPtr<IDXGIAdapter1> adapter = FindAdapterForMonitor(primary);
    if (!g_app.renderer.Init(adapter.Get())) return false;

    MONITORINFO info = {sizeof(info)};
    GetMonitorInfoW(primary, &info);
    const UINT width = UINT(info.rcMonitor.right - info.rcMonitor.left);
    const UINT height = UINT(info.rcMonitor.bottom - info.rcMonitor.top);
    if (!g_app.renderer.CreateSwapChain(g_app.window, width, height)) return false;
    g_app.monitor = nullptr;  // the new device has no desktop pictures yet
    Log("Graphics ready on %s (%ux MSAA)", g_app.renderer.AdapterName(), g_app.renderer.SampleCount());
    return true;
}

bool IsCloaked(HWND window) {
    DWORD cloaked = 0;
    return SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked != 0;
}

// A desktop switch moves focus to a window on the new desktop, which can raise the taskbar over the overlay; and
// if Windows ever decides the overlay belongs to the old desktop it cloaks it. Undo both.
void KeepOverlayOnTop() {
    DWORD cloaked = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(g_app.window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) &&
        (cloaked & DWM_CLOAKED_SHELL)) {
        Log("The desktop switch cloaked the overlay; moving it to the new desktop");
        g_app.desktops.MoveWindowTo(g_app.window, g_app.currentDesktop);
    }
    SetWindowPos(g_app.window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

// After a switch, make sure focus lands on a window of the desktop now showing.
void FocusTopWindow() {
    const HWND foreground = GetForegroundWindow();
    if (foreground && foreground != g_app.window && IsWindowVisible(foreground) && !IsCloaked(foreground)) return;
    for (HWND window = GetTopWindow(nullptr); window; window = GetWindow(window, GW_HWNDNEXT)) {
        if (window == g_app.window || !IsWindowVisible(window) || IsIconic(window) || IsCloaked(window)) continue;
        if (GetWindow(window, GW_OWNER) || (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW)) continue;
        if (GetWindowTextLengthW(window) == 0) continue;
        SetForegroundWindow(window);
        return;
    }
}

// While the void is open, every other monitor fades to near black, which also hides Windows' own desktop-switch
// animation there. Click-through, so clicking another monitor still dismisses the void.
void ShowVeils(HMONITOR voidMonitor) {
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR monitor, HDC, LPRECT rect, LPARAM voidMonitor) -> BOOL {
        if (monitor == reinterpret_cast<HMONITOR>(voidMonitor)) return TRUE;
        const HWND veil = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, L"VoidShellVeil",
            L"", WS_POPUP, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top, nullptr, nullptr,
            GetModuleHandleW(nullptr), nullptr);
        if (!veil) return TRUE;
        SetLayeredWindowAttributes(veil, 0, 0, LWA_ALPHA);
        SetWindowPos(veil, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        g_app.veils.push_back(veil);
        return TRUE;
    }, reinterpret_cast<LPARAM>(voidMonitor));
    g_app.veilAlpha = 0;
}

void UpdateVeils(float progress) {
    const float t = std::clamp(progress / 0.7f, 0.0f, 1.0f);
    const BYTE alpha = BYTE(225.0f * t * t * (3.0f - 2.0f * t));
    if (alpha == g_app.veilAlpha) return;
    g_app.veilAlpha = alpha;
    for (const HWND veil : g_app.veils) SetLayeredWindowAttributes(veil, 0, alpha, LWA_ALPHA);
}

void HideVeils() {
    for (const HWND veil : g_app.veils) DestroyWindow(veil);
    g_app.veils.clear();
}

void HideOverlay() {
    const bool switched = g_app.switching;
    HideVeils();
    ShowWindow(g_app.window, SW_HIDE);
    SetWindowDisplayAffinity(g_app.window, WDA_NONE);
    g_app.overlayVisible = false;
    g_app.switching = false;
    g_app.birthTimer = -1.0f;
    g_app.placeholderQueue.clear();
    g_app.scene.Hide();
    g_app.renderer.ReleaseFrameTargets();
    UpdateRainAllowed();
    if (switched) FocusTopWindow();
    UpdateCurrentDesktop(true);
}

// Until a desktop has been visited, its world shows its wallpaper.
void BuildPlaceholder(const GUID& id) {
    std::wstring path;
    for (const DesktopInfo& desktop : g_app.desktopList)
        if (desktop.id == id) path = desktop.wallpaper;
    if (path.empty()) path = GlobalWallpaperPath();
    const UINT width = std::max(1u, g_app.width / 2);
    const UINT height = std::max(1u, g_app.height / 2);
    std::vector<uint8_t>& pixels =
        g_app.wallpaperCache[path + L"|" + std::to_wstring(width) + L"x" + std::to_wstring(height)];
    if (pixels.empty() && !LoadImageFill(path, width, height, pixels)) {
        FillSolid(pixels, width, height, GetSysColor(COLOR_DESKTOP));
    }
    g_app.renderer.StorePixels(id, pixels.data(), width, height);
}

// One wallpaper per frame, so opening the void never waits on image decoding.
void BuildNextPlaceholder() {
    if (g_app.placeholderQueue.empty()) return;
    const GUID id = g_app.placeholderQueue.back();
    g_app.placeholderQueue.pop_back();
    if (!g_app.renderer.HasSnapshot(id)) BuildPlaceholder(id);
}

void OpenVoid() {
    if (g_app.overlayVisible) {
        if (!Busy()) g_app.scene.Open();
        return;
    }
    if (!g_app.graphicsReady) {
        g_app.graphicsReady = InitGraphics();
        if (!g_app.graphicsReady) return;
    }

    POINT cursor;
    GetCursorPos(&cursor);
    const HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFOEXW info = {};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info)) return;
    const RECT rect = info.rcMonitor;
    const UINT width = UINT(rect.right - rect.left);
    const UINT height = UINT(rect.bottom - rect.top);

    ComPtr<ID3D11Texture2D> capture;
    const char* method = "none";
    if (!CaptureMonitor(g_app.renderer.Device(), g_app.renderer.Context(), monitor, capture, &method)) {
        Log("Could not capture the screen");
        return;
    }

    // Read the desktops every time, so ones added, removed or renamed elsewhere show up.
    std::vector<DesktopInfo> desktops;
    GUID current = {};
    if (!g_app.desktops.List(desktops, current) || desktops.empty()) {
        Log("Could not read the virtual desktops; showing just this one");
        desktops.assign(1, DesktopInfo{});
        current = GUID{};
    }
    g_app.desktopList = desktops;
    g_app.currentDesktop = current;

    // Each monitor keeps its own set of desktop pictures, by device name (stable while the monitor is attached).
    g_app.monitor = monitor;
    g_app.renderer.SetMonitor(info.szDevice);
    std::vector<GUID> ids;
    for (const DesktopInfo& desktop : desktops) ids.push_back(desktop.id);
    g_app.renderer.RetainSnapshots(ids);
    if (!g_app.renderer.StoreSnapshot(current, capture.Get(), false)) return;
    D3D11_TEXTURE2D_DESC captureDesc;
    capture->GetDesc(&captureDesc);
    Log("Captured %ux%u with %s; %zu desktops", captureDesc.Width, captureDesc.Height, method, desktops.size());

    g_app.width = width;
    g_app.height = height;
    g_app.placeholderQueue.clear();
    for (const DesktopInfo& desktop : desktops)
        if (!g_app.renderer.HasSnapshot(desktop.id)) g_app.placeholderQueue.push_back(desktop.id);
    g_app.scene.SetAspect(float(width) / float(height));
    g_app.scene.SetDesktops(desktops, current, true);

    // Show the window cloaked, give it the captured desktop, then uncloak, so the swap can't be seen.
    BOOL cloak = TRUE;
    DwmSetWindowAttribute(g_app.window, DWMWA_CLOAK, &cloak, sizeof(cloak));
    SetWindowPos(g_app.window, HWND_TOPMOST, rect.left, rect.top, int(width), int(height),
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    g_app.overlayVisible = true;
    HideBubblePreview();
    ShowVeils(monitor);
    UpdateRainAllowed();  // the frame's rain rests while the void is open
    g_app.scene.SetCanRearrange(g_app.desktops.CanRearrange());
    g_app.scene.Open();
    if (!g_app.renderer.ResizeSwapChain(width, height) || !g_app.renderer.Present(g_app.scene.Frame())) {
        Log("Could not draw the first frame; resetting graphics");
        g_app.graphicsReady = false;
        HideOverlay();
        cloak = FALSE;
        DwmSetWindowAttribute(g_app.window, DWMWA_CLOAK, &cloak, sizeof(cloak));
        return;
    }
    DwmFlush();
    cloak = FALSE;
    DwmSetWindowAttribute(g_app.window, DWMWA_CLOAK, &cloak, sizeof(cloak));
    SetForegroundWindow(g_app.window);
    QueryPerformanceCounter(&g_app.lastTick);
}

void EnterWorld(int index) {
    if (Busy() || g_app.scene.Phase() != VoidPhase::Open || index < 0 || index >= g_app.scene.WorldCount()) return;
    const GUID target = g_app.scene.WorldId(index);
    if (target == g_app.currentDesktop) {
        g_app.scene.Enter(index, false);
        return;
    }

    // Hide the overlay from screen capture so the new desktop can be grabbed through it, then switch underneath.
    SetWindowDisplayAffinity(g_app.window, WDA_EXCLUDEFROMCAPTURE);
    if (!g_app.desktops.Switch(target)) {
        SetWindowDisplayAffinity(g_app.window, WDA_NONE);
        g_app.scene.ShowHint(Hint::SwitchFailed, 3.0f);
        return;
    }
    g_app.switching = true;
    g_app.switchSeen = false;
    g_app.switchCaptured = false;
    g_app.switchTarget = target;
    g_app.switchClock = 0.0f;
    g_app.scene.Enter(index, true);
}

void ReturnToCurrent() {
    const VoidPhase phase = g_app.scene.Phase();
    if (phase == VoidPhase::Opening || phase == VoidPhase::Open) g_app.scene.Enter(g_app.scene.CurrentIndex(), false);
}

void FinishSwitchCapture() {
    g_app.switchCaptured = true;
    SetWindowDisplayAffinity(g_app.window, WDA_NONE);
    KeepOverlayOnTop();
    g_app.scene.SnapshotReady();
}

// Waits for the switch to land, lets the new desktop settle, then grabs it so the zoom ends on what's really there.
void UpdateSwitch(float dt) {
    if (!g_app.switching || g_app.switchCaptured) return;
    g_app.switchClock += dt;
    if (!g_app.switchSeen) {
        GUID now = {};
        if (g_app.desktops.Current(now) && now == g_app.switchTarget) {
            g_app.switchSeen = true;
            g_app.switchSeenAt = g_app.switchClock;
            g_app.currentDesktop = now;
            KeepOverlayOnTop();
        }
    } else if (g_app.switchClock - g_app.switchSeenAt >= kSettleSeconds) {
        ComPtr<ID3D11Texture2D> capture;
        const char* method = "none";
        if (CaptureMonitor(g_app.renderer.Device(), g_app.renderer.Context(), g_app.monitor, capture, &method)) {
            g_app.renderer.StoreSnapshot(g_app.switchTarget, capture.Get(), true);
            Log("Switched desktops in %.0f ms; fresh picture via %s", g_app.switchSeenAt * 1000.0f, method);
        }
        FinishSwitchCapture();
        return;
    }
    if (g_app.switchClock > kSwitchTimeoutSeconds) {
        Log(g_app.switchSeen ? "New desktop was slow to settle; entering with its old picture"
                             : "Desktop switch was not confirmed; entering with the old picture");
        FinishSwitchCapture();
    }
}

void CreateWorld() {
    if (Busy() || g_app.scene.Phase() != VoidPhase::Open) return;
    g_app.scene.PulsePlus();
    const bool switches = g_app.desktops.CreateAlsoSwitches();
    if (switches) SetWindowDisplayAffinity(g_app.window, WDA_EXCLUDEFROMCAPTURE);
    GUID created = {};
    if (!g_app.desktops.Create(created)) {
        if (switches) SetWindowDisplayAffinity(g_app.window, WDA_NONE);
        g_app.scene.ShowHint(Hint::CreateFailed, 3.0f);
        return;
    }
    if (switches) KeepOverlayOnTop();
    Log("Created desktop %ls", GuidToString(created).c_str());

    std::vector<DesktopInfo> desktops;
    GUID ignored = {};
    if (g_app.desktops.List(desktops, ignored) && !desktops.empty()) {
        g_app.desktopList = desktops;
    } else {
        DesktopInfo info;
        info.id = created;
        g_app.desktopList.push_back(info);
    }
    // The world you were on stays marked as current until you actually enter the new one.
    g_app.scene.SetDesktops(g_app.desktopList, g_app.currentDesktop, false);
    BuildPlaceholder(created);
    g_app.bornDesktop = created;
    g_app.birthTimer = kBirthDelaySeconds;
}

void UpdateBirth(float dt) {
    if (g_app.birthTimer < 0.0f) return;
    g_app.birthTimer -= dt;
    if (g_app.birthTimer >= 0.0f) return;
    g_app.birthTimer = -1.0f;
    EnterWorld(g_app.scene.IndexOf(g_app.bornDesktop));
}

void ToggleVoid() {
    if (!g_app.overlayVisible) {
        OpenVoid();
    } else if (Busy()) {
        return;
    } else if (g_app.scene.Phase() == VoidPhase::Closing) {
        g_app.scene.Open();
    } else {
        ReturnToCurrent();
    }
}

void Tick() {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    float dt = float(double(now.QuadPart - g_app.lastTick.QuadPart) / double(g_app.frequency.QuadPart));
    g_app.lastTick = now;
    dt = std::min(dt, 0.05f);

    BuildNextPlaceholder();
    UpdateSwitch(dt);
    UpdateBirth(dt);

    POINT cursor;
    GetCursorPos(&cursor);
    ScreenToClient(g_app.window, &cursor);
    g_app.scene.Update(dt, float(cursor.x), float(cursor.y), g_app.width, g_app.height);
    UpdateVeils(g_app.scene.Progress());

    // Back at progress 0 the last frame is the desktop itself, so hiding the window is seamless.
    if (g_app.scene.Phase() == VoidPhase::Hidden) {
        HideOverlay();
        return;
    }
    if (!g_app.renderer.Present(g_app.scene.Frame())) {
        Log("Present failed; resetting graphics");
        g_app.graphicsReady = false;
        HideOverlay();
    }
}

// Re-reads the desktops after a change made from the void, keeping the worlds where they are.
void RelistDesktops() {
    std::vector<DesktopInfo> desktops;
    GUID current = {};
    if (!g_app.desktops.List(desktops, current) || desktops.empty()) return;
    g_app.desktopList = desktops;
    g_app.currentDesktop = current;
    g_app.scene.SetDesktops(desktops, current, false);
    std::vector<GUID> ids;
    for (const DesktopInfo& desktop : desktops) ids.push_back(desktop.id);
    g_app.renderer.RetainSnapshots(ids);
}

// The remove badge: the first click arms it and says what will happen; a second click within a few seconds removes
// the desktop. Windows moves its windows to the neighbouring desktop, so nothing is closed.
void RemoveWorld(int index) {
    const int count = g_app.scene.WorldCount();
    if (index < 0 || index >= count || count < 2) return;
    const int fallback = index > 0 ? index - 1 : index + 1;
    if (g_app.scene.ArmedRemove() != index) {
        g_app.scene.ArmRemove(index, L"Click × again to remove " + g_app.scene.WorldLabel(index) +
                                         L"  ·  its windows move to " + g_app.scene.WorldLabel(fallback));
        return;
    }
    const GUID removed = g_app.scene.WorldId(index);
    const std::wstring name = g_app.scene.WorldLabel(index);
    if (!g_app.desktops.Remove(removed, g_app.scene.WorldId(fallback))) {
        g_app.scene.ShowHintText(L"Couldn't remove " + name, 3.0f);
        return;
    }
    Log("Removed desktop %ls", GuidToString(removed).c_str());
    g_app.settings.desktopLayouts.erase(GuidToString(removed));
    SaveSettings();
    RelistDesktops();
    g_app.scene.ShowHintText(name + L" removed", 2.5f);
}

void ReorderWorld(int from, int to) {
    const GUID moved = g_app.scene.WorldId(to);  // the scene already shows it in its new place
    if (!g_app.desktops.Move(moved, to)) g_app.scene.ShowHintText(L"Couldn't move that desktop", 3.0f);
    Log("Moved desktop %d to %d", from + 1, to + 1);
    RelistDesktops();  // also renumbers unnamed desktops, as Windows does
}

// Keeps each desktop's ball picture current even when you switch without the void: the desktop on screen is
// captured on every monitor that already has pictures.
void RefreshPictures() {
    if (!g_app.graphicsReady || g_app.overlayVisible || g_app.gameMode || g_app.locked) return;
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR monitor, HDC, LPRECT, LPARAM) -> BOOL {
        MONITORINFOEXW info = {};
        info.cbSize = sizeof(info);
        if (!GetMonitorInfoW(monitor, &info) || !g_app.renderer.HasPicturesFor(info.szDevice)) return TRUE;
        ComPtr<ID3D11Texture2D> capture;
        const char* method = "none";
        if (CaptureMonitor(g_app.renderer.Device(), g_app.renderer.Context(), monitor, capture, &method)) {
            g_app.renderer.SetMonitor(info.szDevice);
            g_app.renderer.StoreSnapshot(g_app.activeDesktop, capture.Get(), false);
        }
        return TRUE;
    }, 0);
}

bool IsShellWindow(HWND window) {
    wchar_t className[64] = {};
    GetClassNameW(window, className, ARRAYSIZE(className));
    for (const wchar_t* shell : {L"Progman", L"WorkerW", L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd"})
        if (wcscmp(className, shell) == 0) return true;
    return false;
}

// Is a game or video filling a screen? Windows reports exclusive fullscreen and presentation mode directly; a
// borderless fullscreen window is one that covers its whole monitor, taskbar included.
bool FindFullscreenApp(HMONITOR& monitor) {
    const HWND foreground = GetForegroundWindow();
    if (!foreground || IsShellWindow(foreground)) return false;
    DWORD process = 0;
    GetWindowThreadProcessId(foreground, &process);
    if (process == GetCurrentProcessId()) return false;

    monitor = MonitorFromWindow(foreground, MONITOR_DEFAULTTONEAREST);
    QUERY_USER_NOTIFICATION_STATE state = QUNS_ACCEPTS_NOTIFICATIONS;
    if (SUCCEEDED(SHQueryUserNotificationState(&state)) &&
        (state == QUNS_RUNNING_D3D_FULL_SCREEN || state == QUNS_PRESENTATION_MODE || state == QUNS_BUSY))
        return true;
    MONITORINFO info = {sizeof(info)};
    RECT rect;
    if (!GetWindowRect(foreground, &rect) || !GetMonitorInfoW(monitor, &info)) return false;
    return rect.left <= info.rcMonitor.left && rect.top <= info.rcMonitor.top && rect.right >= info.rcMonitor.right &&
           rect.bottom >= info.rcMonitor.bottom;
}

// Polled once a second: while a fullscreen app is in front, the shortcut, snapping and the frame on its monitor
// pause, so nothing of ours can interfere with a game.
void UpdateGameMode() {
    if (g_app.overlayVisible) return;  // the void itself is fullscreen
    HMONITOR monitor = nullptr;
    const bool fullscreen = g_app.settings.pauseInFullscreen && FindFullscreenApp(monitor);
    if (!fullscreen) monitor = nullptr;
    if (fullscreen == g_app.gameMode && monitor == g_app.gameMonitor) return;
    if (fullscreen != g_app.gameMode) Log(fullscreen ? "Fullscreen app in front: pausing" : "Fullscreen app gone: resuming");
    g_app.gameMode = fullscreen;
    g_app.gameMonitor = monitor;
    ApplyHotkey();
    ApplySnapping();
    g_app.frame.SetSuppressed(monitor);
}

void ShowSettings() { g_app.settingsWindow.Show(g_app.settings, MakeSettingsContext()); }

// ---- Bubbles in the margin ----

void AddBubble(ShortcutKind kind, const std::wstring& name, const std::wstring& target) {
    Shortcut shortcut;
    shortcut.kind = kind;
    shortcut.name = name;
    shortcut.target = target;
    if (!g_app.frame.AddShortcut(shortcut, g_app.addPoint)) Log("That one is already in the margin");
}

// The windows of the apps that have bubbles, found the way the dock finds an app's windows (on this desktop), and
// shown under the bubbles as the taskbar shows them: running, or in front.
void RefreshBubbleApps() {
    const auto& shortcuts = g_app.frame.Shortcuts();
    std::vector<std::wstring> pins;
    std::vector<int> indices;
    for (size_t i = 0; i < shortcuts.size(); ++i) {
        if (shortcuts[i].kind != ShortcutKind::App) continue;
        pins.push_back(shortcuts[i].target);
        indices.push_back(int(i));
    }
    if (pins != g_app.bubblePins) {
        g_app.bubbleModel.SetPins(pins);  // reading shortcuts and app IDs is the slow part, so only when they change
        g_app.bubblePins = pins;
    }
    g_app.bubbleIndices = indices;
    std::vector<BubbleState> states(shortcuts.size(), BubbleState::Closed);
    if (!pins.empty()) {
        g_app.bubbleModel.Refresh([](HWND window) {
            DWORD cloaked = 0;
            DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
            return cloaked == 0;
        }, GetForegroundWindow(), {});
        const auto& apps = g_app.bubbleModel.Apps();
        for (size_t k = 0; k < indices.size() && k < apps.size(); ++k)
            if (!apps[k].windows.empty()) states[indices[k]] = apps[k].active ? BubbleState::Active : BubbleState::Running;
    }
    g_app.frame.SetShortcutStates(states);
}

// A bubble's app with its open windows, read fresh.
bool BubbleApp(int index, DockApp& app) {
    RefreshBubbleApps();
    const auto& apps = g_app.bubbleModel.Apps();
    for (size_t k = 0; k < g_app.bubbleIndices.size() && k < apps.size(); ++k) {
        if (g_app.bubbleIndices[k] != index) continue;
        app = apps[k];
        app.name = g_app.frame.Shortcuts()[index].name;
        return true;
    }
    return false;
}

// Watch the app bubbles' apps while the frame is up and has any.
void UpdateBubbleWatch() {
    bool apps = false;
    for (const Shortcut& shortcut : g_app.frame.Shortcuts()) apps = apps || shortcut.kind == ShortcutKind::App;
    if (g_app.settings.frame && apps) {
        SetTimer(g_app.window, kBubbleStateTimer, 1500, nullptr);
        RefreshBubbleApps();
    } else {
        KillTimer(g_app.window, kBubbleStateTimer);
    }
}

void HideBubblePreview() {
    g_app.bubblePreview.Hide();
    g_app.bubblePreviewFor = -1;
    g_app.bubbleAwaySince = 0;
}

// Like the taskbar: live thumbnails of an app bubble's open windows, beside the bubble, toward the desktop.
void ShowBubblePreview(int index) {
    const auto& shortcuts = g_app.frame.Shortcuts();
    DockApp app;
    RECT round, monitor;
    int band = -1;
    float scale = 1.0f;
    if (index < 0 || index >= int(shortcuts.size()) || shortcuts[index].kind != ShortcutKind::App ||
        !BubbleApp(index, app) || app.windows.empty() || !g_app.frame.ShortcutAnchor(index, round, band, monitor, scale)) {
        if (g_app.bubblePreview.Shown()) HideBubblePreview();
        return;
    }
    const PreviewSide side = band == 1   ? PreviewSide::Right
                             : band == 2 ? PreviewSide::Left
                             : band == 0 ? PreviewSide::Below
                                         : PreviewSide::Above;
    IconImage icon;
    ShellIcon(shortcuts[index].target, int(lroundf(32.0f * scale)), icon);
    g_app.bubblePreview.Show(app, icon.Empty() ? nullptr : &icon, round, side, monitor, scale);
    g_app.bubblePreviewFor = index;
    g_app.bubbleAwaySince = 0;
}

// Over a bubble for a moment shows its preview; away from both for a moment hides it.
void BubblePreviewTick() {
    const ULONGLONG now = GetTickCount64();
    POINT cursor;
    GetCursorPos(&cursor);
    const bool overPreview = g_app.bubblePreview.Shown() && WindowFromPoint(cursor) == g_app.bubblePreview.Window();
    const int hover = g_app.bubbleHover;
    if (hover >= 0 && !(GetAsyncKeyState(VK_LBUTTON) & 0x8000) && now >= g_app.bubbleBlockedUntil) {
        g_app.bubbleAwaySince = 0;
        const bool showing = g_app.bubblePreview.Shown() && g_app.bubblePreviewFor == hover;
        const ULONGLONG delay = g_app.bubblePreview.Shown() ? 0 : kBubblePreviewDelay;
        if (!showing && hover != g_app.bubbleChecked && now - g_app.bubbleHoverSince >= delay) {
            g_app.bubbleChecked = hover;
            ShowBubblePreview(hover);
        }
    } else if (overPreview) {
        g_app.bubbleAwaySince = 0;
    } else if (g_app.bubblePreview.Shown()) {
        if (!g_app.bubbleAwaySince) g_app.bubbleAwaySince = now;
        if (now - g_app.bubbleAwaySince >= kBubblePreviewHide) HideBubblePreview();
    }
    if (hover < 0 && !g_app.bubblePreview.Shown()) KillTimer(g_app.window, kBubblePreviewTimer);
}

void OnBubbleHover(int index) {
    g_app.bubbleHover = index;
    g_app.bubbleHoverSince = GetTickCount64();
    g_app.bubbleChecked = -1;
    SetTimer(g_app.window, kBubblePreviewTimer, 100, nullptr);
}

// The + on the frame: a folder (Windows' folder picker) or an app (our searchable list of Start's apps).
void ShowAddMenu(POINT at) {
    g_app.addPoint = at;
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, 1, L"Add a folder\u2026");
    AppendMenuW(menu, MF_STRING, 2, L"Add an app\u2026");
    SetForegroundWindow(g_app.window);  // so the menu closes when you click elsewhere
    const UINT command = UINT(TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_TOPALIGN | TPM_CENTERALIGN, at.x,
                                             at.y, 0, g_app.window, nullptr));
    PostMessageW(g_app.window, WM_NULL, 0, 0);
    DestroyMenu(menu);
    if (command == 1) {
        std::wstring name, target;
        if (PickFolder(nullptr, name, target)) AddBubble(ShortcutKind::Folder, name, target);
    } else if (command == 2) {
        g_app.appPicker.Show(MonitorFromPoint(at, MONITOR_DEFAULTTOPRIMARY));
    }
}

// Starts a bubble's app (or opens its folder) as Explorer would, never with our administrator rights.
void LaunchBubble(int index) {
    const auto& shortcuts = g_app.frame.Shortcuts();
    if (index < 0 || index >= int(shortcuts.size())) return;
    std::wstring target = shortcuts[index].target;
    if (target.rfind(L"::", 0) == 0) target = L"shell:" + target;  // a virtual folder such as This PC
    LaunchAsUser(target);
    SetTimer(g_app.window, kBubbleStateSoonTimer, 1500, nullptr);
}

// A click on a bubble. An app that's already open acts as its taskbar icon would: it comes to the front, or is
// minimized if it's in front already; with several windows, each click brings up the next.
void OpenBubble(int index) {
    const auto& shortcuts = g_app.frame.Shortcuts();
    if (index < 0 || index >= int(shortcuts.size())) return;
    HideBubblePreview();
    g_app.bubbleBlockedUntil = GetTickCount64() + kBubbleAfterClick;
    DockApp app;
    if (shortcuts[index].kind == ShortcutKind::App && BubbleApp(index, app) && !app.windows.empty()) {
        ToggleAppWindows(app.windows);
        SetTimer(g_app.window, kBubbleStateSoonTimer, 250, nullptr);
        return;
    }
    std::wstring target = shortcuts[index].target;
    if (target.rfind(L"::", 0) == 0) target = L"shell:" + target;  // a virtual folder such as This PC
    LaunchAsUser(target);
}

void ShowBubbleMenu(int index, POINT at) {
    const auto& shortcuts = g_app.frame.Shortcuts();
    if (index < 0 || index >= int(shortcuts.size())) return;
    const Shortcut shortcut = shortcuts[index];
    const bool onDisk = GetFileAttributesW(shortcut.target.c_str()) != INVALID_FILE_ATTRIBUTES;
    DockApp app;
    const bool running = shortcut.kind == ShortcutKind::App && BubbleApp(index, app) && !app.windows.empty();
    HideBubblePreview();
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | MF_DISABLED, 0, shortcut.name.c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, running ? kShortcutNew : kShortcutOpen, running ? L"New window" : L"Open");
    if (running)
        AppendMenuW(menu, MF_STRING, kShortcutClose, app.windows.size() > 1 ? L"Close all windows" : L"Close window");
    if (onDisk) AppendMenuW(menu, MF_STRING, kShortcutLocation, L"Show in its folder");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kShortcutRemove, L"Remove from the margin");
    SetForegroundWindow(g_app.window);
    const UINT command = UINT(TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, at.x, at.y, 0, g_app.window, nullptr));
    PostMessageW(g_app.window, WM_NULL, 0, 0);
    DestroyMenu(menu);
    if (command == kShortcutOpen) OpenBubble(index);
    if (command == kShortcutNew) LaunchBubble(index);
    if (command == kShortcutClose) {
        for (const DockWindow& window : app.windows) PostMessageW(window.window, WM_CLOSE, 0, 0);
        SetTimer(g_app.window, kBubbleStateSoonTimer, 500, nullptr);
    }
    if (command == kShortcutRemove) g_app.frame.RemoveShortcut(index);
    if (command == kShortcutLocation) {
        PIDLIST_ABSOLUTE item = nullptr;
        if (SUCCEEDED(SHParseDisplayName(shortcut.target.c_str(), nullptr, &item, 0, nullptr))) {
            SHOpenFolderAndSelectItems(item, 0, nullptr, 0);  // Explorer opens it, so it's never elevated
            CoTaskMemFree(item);
        }
    }
}

// A dot on the dock was clicked: go to that desktop.
void SwitchToDesktop(int index) {
    std::vector<DesktopInfo> desktops;
    GUID current = {};
    if (g_app.overlayVisible || !g_app.desktops.List(desktops, current) || index < 0 || index >= int(desktops.size()))
        return;
    if (desktops[index].id == current) return;
    if (g_app.desktops.Switch(desktops[index].id)) UpdateCurrentDesktop(true);
}

// Once per session: say why a window didn't snap, from the tray.
void ShowAdminHint() {
    if (g_app.adminHintShown || g_app.elevated) return;
    g_app.adminHintShown = true;
    NOTIFYICONDATAW data = {sizeof(data)};
    data.hWnd = g_app.window;
    data.uID = 1;
    data.uFlags = NIF_INFO;
    data.dwInfoFlags = NIIF_INFO;
    wcscpy_s(data.szInfoTitle, L"That window can't be snapped yet");
    wcscpy_s(data.szInfo, L"It runs as administrator. Turn on \"Run as administrator\" in Void Shell settings to snap it.");
    Shell_NotifyIconW(NIM_MODIFY, &data);
}

bool SetHotkey(UINT modifiers, UINT key) {
    UnregisterHotKey(g_app.window, kHotkeyId);
    if (!RegisterHotKey(g_app.window, kHotkeyId, modifiers | MOD_NOREPEAT, key)) return false;
    UnregisterHotKey(g_app.window, kHotkeyId);  // registered for real once recording ends
    g_app.settings.hotkeyModifiers = modifiers;
    g_app.settings.hotkeyKey = key;
    SaveSettings();
    UpdateTrayTip();
    Log("Shortcut is now %ls", HotkeyText(modifiers, key).c_str());
    return true;
}

void ApplyStartupSettings() {
    g_app.startupNote = ApplyStartup(g_app.settings.startAtLogin, g_app.settings.runAsAdmin);
    if (!g_app.startupNote.empty()) Log("Startup: %ls", g_app.startupNote.c_str());
}

void OnSettingChanged(SettingId id, int part) {
    Settings& s = g_app.settings;
    switch (id) {
        case SettingId::PauseInFullscreen:
            s.pauseInFullscreen = !s.pauseInFullscreen;
            UpdateGameMode();
            break;
        case SettingId::Frame:
            s.frame = !s.frame;
            g_app.frame.SetEnabled(s.frame);
            ApplyDock();
            UpdateRainAllowed();
            UpdateBubbleWatch();
            break;
        case SettingId::SystemStats:
            s.systemStats = !s.systemStats;
            ApplyFrameStyle();
            UpdateRainAllowed();
            break;
        case SettingId::ShowSockets:
            s.showSockets = !s.showSockets;
            ApplyFrameStyle();
            break;
        case SettingId::FrameStyle:
            s.frameWide = part == 1;
            ApplyFrameStyle();
            break;
        case SettingId::FrameMargin:
            s.frameMargin = std::clamp(part, 0, 2);
            ApplyFrameStyle();
            break;
        case SettingId::Rain:
            s.matrixRain = !s.matrixRain;
            ApplyFrameStyle();
            break;
        case SettingId::RainColor:
            s.rainColor = std::clamp(part, 0, 2);
            ApplyFrameStyle();
            break;
        case SettingId::FrameMonitors:
            s.frameAllMonitors = part == 0;
            g_app.frame.SetAllMonitors(s.frameAllMonitors);
            break;
        case SettingId::Dock:
            s.dock = !s.dock;
            ApplyFrameStyle();  // the bottom band grows or shrinks
            ApplyDock();
            break;
        case SettingId::DockDesktops:
            s.dockAllDesktops = part == 1;
            ApplyDock();
            break;
        case SettingId::HideTaskbar:
            s.hideWindowsTaskbar = !s.hideWindowsTaskbar;
            ApplyTaskbar();
            break;
        case SettingId::ResetPins:
            s.dockPins = TaskbarPinnedShortcuts();
            g_app.dock.SetPins(s.dockPins);
            break;
        case SettingId::Snap:
            s.snap = !s.snap;
            ApplySnapping();
            break;
        case SettingId::AutoPlace:
            s.autoPlace = !s.autoPlace;
            ApplySnapping();
            break;
        case SettingId::DesktopLayout:
            ChooseLayout(part);
            return;
        case SettingId::DefaultLayout:
            s.defaultLayout = part;
            g_app.frame.SetLayout(CurrentLayout());
            g_app.snapper.Reflow();
            break;
        case SettingId::ResetLayouts:
            s.desktopLayouts.clear();
            g_app.frame.SetLayout(CurrentLayout());
            g_app.snapper.Reflow();
            break;
        case SettingId::StartAtLogin:
            s.startAtLogin = !s.startAtLogin;
            ApplyStartupSettings();
            break;
        case SettingId::RunAsAdmin:
            s.runAsAdmin = !s.runAsAdmin;
            ApplyStartupSettings();
            break;
        case SettingId::RestartElevated:
            SaveSettings();
            // The elevated copy waits for this one to exit, then takes over.
            if (RestartElevated()) {
                DestroyWindow(g_app.window);
                return;
            }
            break;
        case SettingId::OpenLog:
            ShellExecuteW(nullptr, L"open", (ExeDirectory() + L"void-shell.log").c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return;
        case SettingId::OpenFolder:
            ShellExecuteW(nullptr, L"open", ExeDirectory().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return;
        default:
            return;
    }
    SaveSettings();
    RefreshSettingsWindow();
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == g_app.taskbarCreatedMessage && message != 0) {
        // Explorer restarted: it forgot the tray icon and the frame's reserved space.
        AddTrayIcon();
        g_app.frame.ScheduleRebuild();
        return 0;
    }
    switch (message) {
        case kOpenVoidMessage:
            OpenVoid();
            return 0;
        case kOpenSettingsMessage:
            ShowSettings();
            return 0;
        case kDockSettingsMessage:
            g_app.settingsWindow.ShowTab(2, g_app.settings, MakeSettingsContext());
            return 0;
        case kSwitchDesktopMessage:
            SwitchToDesktop(int(wParam));
            return 0;
        case kDockSlotMessage:
            UpdateDockSlot();
            return 0;
        case kStatsMessage:
            g_app.frame.SetStats(g_app.systemMonitor.Latest());
            return 0;
        case kOpenStatsMessage:
            LaunchAsUser(L"taskmgr.exe");
            return 0;
        case kAddShortcutMessage:
            ShowAddMenu({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
            return 0;
        case kOpenShortcutMessage:
            OpenBubble(int(wParam));
            return 0;
        case kShortcutMenuMessage:
            ShowBubbleMenu(int(wParam), {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
            return 0;
        case kLaunchShortcutMessage:
            LaunchBubble(int(wParam));
            return 0;
        case kLayoutMenuMessage:
            TrackMenu(BuildLayoutMenu(), {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}, TPM_TOPALIGN | TPM_CENTERALIGN);
            return 0;
        case WM_TIMER:
            if (wParam == kReflowTimer) {
                KillTimer(hwnd, kReflowTimer);
                g_app.snapper.Reflow();
            } else if (wParam == kDesktopNameTimer) {
                UpdateCurrentDesktop();
                UpdateGameMode();
            } else if (wParam == kPictureTimer) {
                KillTimer(hwnd, kPictureTimer);
                RefreshPictures();
            } else if (wParam == kPeriodicPictureTimer) {
                RefreshPictures();
            } else if (wParam == kBubbleStateTimer) {
                RefreshBubbleApps();
            } else if (wParam == kBubbleStateSoonTimer) {
                KillTimer(hwnd, kBubbleStateSoonTimer);
                RefreshBubbleApps();
            } else if (wParam == kBubblePreviewTimer) {
                BubblePreviewTick();
            } else if (wParam == kBubblePreviewRefresh) {
                KillTimer(hwnd, kBubblePreviewRefresh);
                DockApp app;
                if (g_app.bubblePreview.Shown() && BubbleApp(g_app.bubblePreviewFor, app)) {
                    g_app.bubblePreview.Update(app, nullptr);
                } else {
                    HideBubblePreview();
                }
            }
            return 0;
        case WM_DISPLAYCHANGE:
            g_app.frame.ScheduleRebuild();
            return 0;
        case WM_ENDSESSION:
            if (wParam) {
                g_app.frame.RemoveAppBars();
                RestoreTaskbar();
            }
            return 0;
        case WM_HOTKEY:
            if (wParam == kHotkeyId) ToggleVoid();
            return 0;
        case kTrayMessage:
            switch (LOWORD(lParam)) {
                case WM_LBUTTONUP:
                    OpenVoid();
                    break;
                case WM_RBUTTONUP:
                case WM_CONTEXTMENU:
                    ShowTrayMenu();
                    break;
            }
            return 0;
        case WM_COMMAND: {
            const UINT command = LOWORD(wParam);
            if (command == kMenuOpen) OpenVoid();
            if (command == kMenuSettings) ShowSettings();
            if (command == kMenuQuit) DestroyWindow(hwnd);
            if (command == kMenuFrame) OnSettingChanged(SettingId::Frame, 0);
            if (command == kMenuSnap) OnSettingChanged(SettingId::Snap, 0);
            if (command == kMenuAutoPlace) OnSettingChanged(SettingId::AutoPlace, 0);
            if (command >= kMenuLayoutBase && command < kMenuLayoutBase + ZoneLayouts().size())
                ChooseLayout(int(command - kMenuLayoutBase));
            return 0;
        }
        case WM_KEYDOWN:
            if (g_app.overlayVisible && !Busy()) {
                if (wParam == VK_ESCAPE || wParam == VK_RETURN) ReturnToCurrent();
                if (wParam >= '1' && wParam <= '9') EnterWorld(int(wParam - '1'));
                if (wParam >= VK_NUMPAD1 && wParam <= VK_NUMPAD9) EnterWorld(int(wParam - VK_NUMPAD1));
            }
            return 0;
        case WM_LBUTTONDOWN:
            if (g_app.overlayVisible && !Busy()) {
                SetCapture(hwnd);  // keep getting the mouse while a world is dragged
                g_app.scene.PointerDown(float(GET_X_LPARAM(lParam)), float(GET_Y_LPARAM(lParam)), g_app.width,
                                        g_app.height);
            }
            return 0;
        case WM_LBUTTONUP:
            if (GetCapture() == hwnd) ReleaseCapture();
            if (g_app.overlayVisible && !Busy()) {
                const SceneAction action = g_app.scene.PointerUp(float(GET_X_LPARAM(lParam)),
                                                                 float(GET_Y_LPARAM(lParam)), g_app.width, g_app.height);
                switch (action.type) {
                    case SceneAction::Type::Enter: EnterWorld(action.index); break;
                    case SceneAction::Type::Create: CreateWorld(); break;
                    case SceneAction::Type::Remove: RemoveWorld(action.index); break;
                    case SceneAction::Type::Reorder: ReorderWorld(action.index, action.to); break;
                    default: break;
                }
            }
            return 0;
        case WM_WTSSESSION_CHANGE:
            if (wParam == WTS_SESSION_LOCK || wParam == WTS_SESSION_UNLOCK) {
                g_app.locked = wParam == WTS_SESSION_LOCK;
                UpdateRainAllowed();
            }
            return 0;
        case WM_POWERBROADCAST:
            if (wParam == PBT_APMPOWERSTATUSCHANGE) UpdateRainAllowed();
            return TRUE;
        case WM_ACTIVATE:
            // If something else takes focus while the void is simply open, get out of the way at once. Our own
            // desktop switches move focus on purpose, so those are ignored.
            if (LOWORD(wParam) == WA_INACTIVE && g_app.overlayVisible && !Busy() &&
                g_app.scene.Phase() != VoidPhase::Closing) {
                HideOverlay();
            }
            return 0;
        case WM_CLOSE:
            // Alt+F4 leaves the void instead of quitting; quit from the tray menu.
            if (g_app.overlayVisible && !Busy()) HideOverlay();
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            ValidateRect(hwnd, nullptr);
            return 0;
        case WM_DESTROY:
            UnregisterHotKey(hwnd, kHotkeyId);
            KillTimer(hwnd, kDesktopNameTimer);
            KillTimer(hwnd, kPeriodicPictureTimer);
            KillTimer(hwnd, kPictureTimer);
            WTSUnRegisterSessionNotification(hwnd);
            KillTimer(hwnd, kReflowTimer);
            g_app.settingsWindow.Close();
            g_app.appPicker.Close();
            g_app.bubblePreview.Shutdown();
            for (const UINT_PTR timer : {kBubbleStateTimer, kBubbleStateSoonTimer, kBubblePreviewTimer, kBubblePreviewRefresh})
                KillTimer(hwnd, timer);
            HideVeils();
            g_app.snapper.Shutdown();
            g_app.dock.Shutdown();
            g_app.systemMonitor.Stop();
            RestoreTaskbar();
            g_app.frame.Shutdown();  // gives the frame's screen space back
            RemoveTrayIcon();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

// If Void Shell ever crashes, give the frame's reserved screen space back so windows don't stay squeezed.
LONG WINAPI OnCrash(EXCEPTION_POINTERS*) {
    g_app.frame.RemoveAppBars();
    if (g_app.settings.taskbarRestore >= 0) SetTaskbarAutoHide(g_app.settings.taskbarRestore == 1);
    return EXCEPTION_CONTINUE_SEARCH;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    // The manifest already asks for per-monitor DPI awareness; this covers a build without it.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::wstring> args(argv + 1, argv + argc);
    LocalFree(argv);
    for (size_t i = 0; i + 1 < args.size(); ++i) {
        if (args[i] == L"--render-test") {
            const bool useCapture = std::find(args.begin(), args.end(), L"--capture") != args.end();
            return RunRenderTest(args[i + 1], useCapture);
        }
        if (args[i] == L"--desktops-test") return RunDesktopsTest(args[i + 1]);
        if (args[i] == L"--frame-test") return RunFrameTest(args[i + 1]);
        if (args[i] == L"--settings-test") return RunSettingsTest(args[i + 1]);
        if (args[i] == L"--startup-test") return RunStartupTest(args[i + 1]);
        if (args[i] == L"--dock-test") return RunDockTest(args[i + 1]);
        if (args[i] == L"--sysinfo-test") return RunSysInfoTest(args[i + 1]);
        if (args[i] == L"--bubbles-test") return RunBubblesTest(args[i + 1]);
    }

    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\VoidShellPrototype");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // A copy restarted as administrator waits for the one it replaces to exit.
        const bool replacing = std::find(args.begin(), args.end(), L"--replace") != args.end();
        const DWORD waited = replacing ? WaitForSingleObject(mutex, 10000) : WAIT_TIMEOUT;
        if (waited != WAIT_OBJECT_0 && waited != WAIT_ABANDONED) {
            MessageBoxW(nullptr, L"VenomMind is already running. Look for its mark in the system tray.", kAppName,
                        MB_OK | MB_ICONINFORMATION);
            return 0;
        }
    }

    LogOpen((ExeDirectory() + L"void-shell.log").c_str());
    g_app.elevated = IsElevated();
    Log("Void Shell starting%s", g_app.elevated ? " as administrator" : "");
    // The shell's desktop API and the wallpaper decoder are COM; this thread is their apartment.
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    g_app.desktops.Init();
    QueryPerformanceFrequency(&g_app.frequency);
    g_app.icon = venom::CreateMarkIcon(GetSystemMetrics(SM_CXSMICON));

    WNDCLASSEXW windowClass = {sizeof(windowClass)};
    windowClass.lpfnWndProc = WndProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hIcon = g_app.icon;
    windowClass.lpszClassName = kWindowClass;
    RegisterClassExW(&windowClass);

    // One hidden popup does everything: it owns the tray icon and hotkey, and becomes the overlay.
    // WS_EX_TOOLWINDOW keeps it out of the taskbar and Alt+Tab, and normally visible across desktop switches
    // (KeepOverlayOnTop handles the case where it isn't).
    g_app.window = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, kWindowClass, kAppName, WS_POPUP, 0, 0, 640,
                                   480, nullptr, nullptr, instance, nullptr);
    if (!g_app.window) {
        Log("CreateWindowEx failed: %lu", GetLastError());
        return 1;
    }
    g_app.taskbarCreatedMessage = RegisterWindowMessageW(L"TaskbarCreated");

    g_app.graphicsReady = InitGraphics();
    if (!g_app.graphicsReady) {
        MessageBoxW(nullptr, L"Direct3D 11 could not start. Details are in void-shell.log next to the exe.",
                    kAppName, MB_OK | MB_ICONERROR);
        return 1;
    }

    // The shell frame, zone snapping and the rest, as last left.
    SetUnhandledExceptionFilter(OnCrash);
    g_app.settingsPath = ExeDirectory() + L"void-shell.ini";
    g_app.settings.Load(g_app.settingsPath);
    ShellFrame::Callbacks callbacks;
    // Posted rather than called, so nothing runs inside the frame's own window procedure.
    callbacks.openVoid = [] { PostMessageW(g_app.window, kOpenVoidMessage, 0, 0); };
    callbacks.openSettings = [] { PostMessageW(g_app.window, kOpenSettingsMessage, 0, 0); };
    callbacks.chooseLayout = [](POINT at) { PostMessageW(g_app.window, kLayoutMenuMessage, 0, MAKELPARAM(at.x, at.y)); };
    // Snapped windows follow their zones once the work area has settled.
    callbacks.workAreaChanged = [] { SetTimer(g_app.window, kReflowTimer, 350, nullptr); };
    callbacks.openStats = [] { PostMessageW(g_app.window, kOpenStatsMessage, 0, 0); };
    callbacks.addShortcut = [](POINT at) { PostMessageW(g_app.window, kAddShortcutMessage, 0, MAKELPARAM(at.x, at.y)); };
    callbacks.openShortcut = [](int index) { PostMessageW(g_app.window, kOpenShortcutMessage, WPARAM(index), 0); };
    callbacks.shortcutMenu = [](int index, POINT at) {
        PostMessageW(g_app.window, kShortcutMenuMessage, WPARAM(index), MAKELPARAM(at.x, at.y));
    };
    callbacks.shortcutsChanged = [](const std::vector<Shortcut>& shortcuts) {
        g_app.settings.shortcuts = shortcuts;
        SaveSettings();
        UpdateBubbleWatch();
    };
    callbacks.hoverShortcut = [](int index) { OnBubbleHover(index); };
    callbacks.bubblePressed = [] {
        HideBubblePreview();
        g_app.bubbleBlockedUntil = GetTickCount64() + kBubbleAfterClick;
    };
    callbacks.launchShortcut = [](int index) { PostMessageW(g_app.window, kLaunchShortcutMessage, WPARAM(index), 0); };
    callbacks.barsChanged = [] {
        if (g_app.dockSlotPending) return;
        g_app.dockSlotPending = true;
        PostMessageW(g_app.window, kDockSlotMessage, 0, 0);
    };
    g_app.frame.Init(instance, g_app.window, callbacks);
    g_app.frame.SetShortcuts(g_app.settings.shortcuts);
    WindowPreview::Callbacks bubblePreviewCallbacks;
    bubblePreviewCallbacks.picked = [] {
        g_app.bubbleBlockedUntil = GetTickCount64() + kBubbleAfterClick;
        SetTimer(g_app.window, kBubbleStateSoonTimer, 250, nullptr);
    };
    bubblePreviewCallbacks.closed = [] { SetTimer(g_app.window, kBubblePreviewRefresh, 400, nullptr); };
    g_app.bubblePreview.Init(instance, bubblePreviewCallbacks);
    g_app.appPicker.Init(instance, [](const InstalledApp& app) { AddBubble(ShortcutKind::App, app.name, app.target); });

    // The dock starts with what is pinned to the Windows taskbar; after that its pins are its own.
    if (!g_app.settings.dockSeeded) {
        g_app.settings.dockPins = TaskbarPinnedShortcuts();
        g_app.settings.dockSeeded = true;
        SaveSettings();
    }
    Dock::Callbacks dockCallbacks;
    dockCallbacks.switchDesktop = [](int index) { PostMessageW(g_app.window, kSwitchDesktopMessage, WPARAM(index), 0); };
    dockCallbacks.pinsChanged = [](const std::vector<std::wstring>& pins) {
        g_app.settings.dockPins = pins;
        SaveSettings();
        RefreshSettingsWindow();
    };
    dockCallbacks.onSomeDesktop = [](HWND window) {
        GUID desktop = {};
        return g_app.desktops.WindowDesktop(window, desktop) && desktop != GUID{};
    };
    dockCallbacks.openSettings = [] { PostMessageW(g_app.window, kDockSettingsMessage, 0, 0); };
    dockCallbacks.placed = [](const RECT* area) { g_app.frame.SetDockArea(area); };  // bubbles keep clear of it
    if (!g_app.dock.Init(instance, g_app.window, dockCallbacks)) Log("Could not create the dock");
    g_app.dock.SetPins(g_app.settings.dockPins);
    g_app.snapper.Init(instance);
    g_app.snapper.SetLayoutResolver(LayoutForWindow);
    g_app.snapper.SetAccessDeniedHandler([](HWND) { ShowAdminHint(); });

    SettingsWindow::Callbacks settingsCallbacks;
    settingsCallbacks.changed = OnSettingChanged;
    settingsCallbacks.setHotkey = SetHotkey;
    settingsCallbacks.recording = [](bool recording) {
        g_app.recordingHotkey = recording;
        ApplyHotkey();
    };
    g_app.settingsWindow.Init(instance, g_app.icon, settingsCallbacks);

    WNDCLASSEXW veilClass = {sizeof(veilClass)};
    veilClass.lpfnWndProc = DefWindowProcW;
    veilClass.hInstance = instance;
    veilClass.hbrBackground = CreateSolidBrush(RGB(3, 5, 14));
    veilClass.lpszClassName = L"VoidShellVeil";
    RegisterClassExW(&veilClass);

    // Bring the startup entries in line (this also finishes the admin task after a restart as administrator).
    const StartupStatus startup = ReadStartup();
    if (g_app.settings.startAtLogin || startup.runEntry || startup.task) ApplyStartupSettings();

    AddTrayIcon();
    ApplySettings();
    UpdateBubbleWatch();
    UpdateTrayTip();
    SetTimer(g_app.window, kDesktopNameTimer, 1000, nullptr);
    SetTimer(g_app.window, kPeriodicPictureTimer, kPeriodicPictureMs, nullptr);
    WTSRegisterSessionNotification(g_app.window, NOTIFY_FOR_THIS_SESSION);  // lock / unlock pauses the rain
    if (!g_app.hotkeyRegistered) {
        const std::wstring message = HotkeyText(g_app.settings.hotkeyModifiers, g_app.settings.hotkeyKey) +
                                     L" is already used by another app.\nYou can pick another shortcut in Settings, "
                                     L"or open the void from the tray orb.";
        MessageBoxW(nullptr, message.c_str(), kAppName, MB_OK | MB_ICONWARNING);
    }

    // Sleep in GetMessage while hidden (no CPU or GPU use); spin a vsync-paced render loop while visible.
    MSG msg = {};
    bool running = true;
    while (running) {
        if (!g_app.overlayVisible) {
            if (GetMessageW(&msg, nullptr, 0, 0) <= 0) break;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            continue;
        }
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                running = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (running && g_app.overlayVisible) Tick();
    }

    g_app.renderer.Reset();
    g_app.desktops = VirtualDesktops();  // release the shell connection while COM is still up
    CoUninitialize();
    if (mutex) CloseHandle(mutex);
    Log("Void Shell stopped");
    return int(msg.wParam);
}
