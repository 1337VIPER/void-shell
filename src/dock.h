#pragma once

#include "common.h"  // windows.h first

#include <dwmapi.h>

#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "audio.h"
#include "canvas.h"
#include "icons.h"

// ---- Model: which apps the dock shows ----

struct DockWindow {
    HWND window;
    std::wstring title;
    int z;  // 0 is the app's window nearest the top
};

// One app on the dock: pinned, running, or both. Windows are grouped the way the Windows taskbar groups them:
// by AppUserModelID when the app sets one, otherwise by its exe.
struct DockApp {
    std::wstring appId;       // AppUserModelID, if known
    std::wstring exe;         // lower-case exe path, if known
    std::wstring name;
    std::wstring launch;      // what starts it: the pinned shortcut, the exe, or shell:AppsFolder\<id>
    std::wstring iconSource;  // shell parsing name for its icon
    HWND iconWindow = nullptr;  // fallback icon source when the shell has none
    bool pinned = false;
    bool active = false;     // owns the foreground window
    bool attention = false;  // a window is flashing for attention
    std::vector<DockWindow> windows;  // in a stable order, so clicking cycles through them all
};

class DockModel {
public:
    void SetPins(const std::vector<std::wstring>& paths);  // shortcuts, exes or shell:AppsFolder\<id>
    // Re-reads open windows. `include` decides which count (for example only the current desktop's).
    void Refresh(const std::function<bool(HWND)>& include, HWND foreground, const std::set<HWND>& flashing);
    const std::vector<DockApp>& Apps() const { return apps_; }
    std::vector<std::wstring> PinPaths() const;
    bool Pin(const DockApp& app);
    void Unpin(const DockApp& app);
    // The same test the Windows taskbar uses: visible, unowned (or explicitly an app window), titled.
    static bool IsTaskbarWindow(HWND window);

private:
    struct PinEntry {
        std::wstring path;
        std::wstring appId;
        std::wstring exe;
        std::wstring name;
    };
    struct Known {
        std::wstring name;
        bool inAppsFolder = false;  // its AppUserModelID can be launched from shell:AppsFolder
    };
    static PinEntry ReadPin(const std::wstring& path);
    const Known& Lookup(const std::wstring& appId, const std::wstring& exe);

    std::vector<PinEntry> pins_;
    std::vector<std::wstring> runningOrder_;  // so running apps keep their place between refreshes
    std::map<std::wstring, Known> known_;
    std::vector<DockApp> apps_;
};

// ---- View: layout and painting, pure so the self-test can draw it ----

enum class DockPart { None, Start, Search, App, Desktop, Volume, Clock };

struct DockHit {
    DockPart part = DockPart::None;
    int index = -1;
    bool operator==(const DockHit& other) const { return part == other.part && index == other.index; }
    bool operator!=(const DockHit& other) const { return !(*this == other); }
};

struct DockLayout {
    int width = 0;  // the whole window, including room for the glow
    int height = 0;
    RECT pill = {};
    RECT start = {};
    RECT search = {};
    std::vector<RECT> apps;
    std::vector<RECT> desktops;
    RECT volume = {};
    RECT clock = {};
    std::vector<LONG> separators;  // x positions
    int iconSize = 0;
    float scale = 1.0f;
};

struct DockState {
    int currentDesktop = 0;
    float volume = 0.5f;
    bool muted = false;
    bool hasAudio = true;
    std::wstring time;
    std::wstring date;
    DockHit hover;
    float pulse = 0.0f;  // 0..1, for apps asking for attention
};

// The dock's height at a display scale; the frame's bottom band is at least this tall while the dock is on.
int DockBandHeight(float scale);
// The widest the clock gets in this locale, so the dock doesn't change width as the minutes pass.
int DockClockWidth(float scale);
DockLayout LayoutDock(int apps, int desktops, float scale, int maxWidth, int clockWidth);
DockHit DockHitTest(const DockLayout& layout, POINT point);
RECT DockPartRect(const DockLayout& layout, DockHit hit);
// `icons` lines up with `apps`; a null icon paints a letter tile.
void PaintDock(Canvas& canvas, const DockLayout& layout, const std::vector<DockApp>& apps,
               const std::vector<const IconImage*>& icons, const DockState& state);

// The popup above the dock: a live thumbnail per window of an app (DWM draws them into `thumbs`), or just a label
// when there are no windows to show.
struct PreviewLayout {
    int width = 0;
    int height = 0;
    std::vector<RECT> cells;
    std::vector<RECT> thumbs;
    std::vector<RECT> closes;
};
constexpr int kMaxPreviewWindows = 6;
PreviewLayout LayoutPreview(const DockApp& app, float scale);
int PreviewCellAt(const PreviewLayout& layout, POINT point, bool* onClose);
// `live[i]` says DWM shows window i's thumbnail; otherwise `icon` is drawn there instead.
void PaintPreview(Canvas& canvas, const PreviewLayout& layout, const DockApp& app, const IconImage* icon,
                  const std::vector<bool>& live, int hoverCell, bool hoverClose, float scale);

// ---- The preview window, shared by the dock and the margin's app bubbles ----

enum class PreviewSide { Above, Below, Left, Right };

// Where a preview of `size` goes beside `anchor` (screen pixels), kept inside `bounds`.
RECT PlacePreview(SIZE size, const RECT& anchor, PreviewSide side, const RECT& bounds, int gap);

// A popup with a live thumbnail per window of an app, or just a label when there are none. Click a thumbnail to go
// to that window, its x (or a middle click) to close it.
class WindowPreview {
public:
    struct Callbacks {
        std::function<void()> picked;  // a window was brought to the front from the preview
        std::function<void()> closed;  // a window was asked to close: refresh and call Update shortly
    };
    bool Init(HINSTANCE instance, Callbacks callbacks);
    void Shutdown();
    void Show(const DockApp& content, const IconImage* icon, const RECT& anchor, PreviewSide side, const RECT& bounds,
              float scale);
    // The same app, re-read: lays out again if its windows changed (hides once none are left), else repaints titles.
    void Update(const DockApp& content, const IconImage* icon);
    void Hide();
    bool Shown() const { return shown_; }
    HWND Window() const { return window_; }

private:
    static LRESULT CALLBACK Proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT Handle(UINT message, WPARAM wParam, LPARAM lParam);
    void Paint();
    void PlaceThumbnails();
    void ReleaseThumbnails();

    HWND window_ = nullptr;
    Callbacks callbacks_;
    Canvas canvas_;
    PreviewLayout layout_;
    DockApp content_;
    IconImage icon_;
    std::vector<HTHUMBNAIL> thumbnails_;
    std::vector<bool> live_;
    int hover_ = -1;
    bool hoverClose_ = false;
    bool tracking_ = false;
    bool shown_ = false;
    RECT anchor_ = {};
    PreviewSide side_ = PreviewSide::Above;
    RECT bounds_ = {};
    float scale_ = 1.0f;
};

// Brings a window to the front (restoring it if minimized).
void ActivateWindow(HWND window);
// What clicking an app on the Windows taskbar does with its open windows: one window comes to the front, or is
// minimized if it already is; several are cycled through, most recent first.
void ToggleAppWindows(const std::vector<DockWindow>& windows);
// A Start entry for a desktop app without an app ID is named by its exe's path under a known folder
// ("{GUID}\Vendor\app.exe"); this turns that into the real path. Empty if `id` isn't one.
std::wstring ExeFromAppsFolderId(const std::wstring& id);

// ---- The dock window ----

// A taskbar in the frame's bottom margin: Start and search, pinned and running apps with live previews, the
// virtual desktops, volume and the clock. Lives on the main monitor.
class Dock {
public:
    struct Callbacks {
        std::function<void(int)> switchDesktop;
        std::function<void(const std::vector<std::wstring>&)> pinsChanged;
        // True when a window cloaked by the shell is on some virtual desktop (rather than a hidden system window).
        std::function<bool(HWND)> onSomeDesktop;
        std::function<void()> openSettings;
        std::function<void(const RECT*)> placed;  // where the dock now is on screen, or null when hidden
    };

    bool Init(HINSTANCE instance, HWND voidOverlay, Callbacks callbacks);
    void SetPins(const std::vector<std::wstring>& pins);
    void SetEnabled(bool enabled);
    void SetAllDesktops(bool all);
    // Where to sit: the main monitor's bottom band (screen pixels), or null when there is none.
    void SetSlot(const RECT* slot, float scale);
    void SetDesktops(const std::vector<std::wstring>& names, int current);
    void BringToTop();
    void Shutdown();
    // For the self-test: what the dock would show right now.
    const std::vector<DockApp>& Apps() const { return model_.Apps(); }

private:
    static LRESULT CALLBACK DockProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleDock(UINT message, WPARAM wParam, LPARAM lParam);

    bool Visible() const { return enabled_ && haveSlot_; }
    void OnShellEvent(WPARAM code, HWND window);
    void ScheduleRefresh(UINT delay = 80);
    void Refresh();
    void Relayout();
    void Repaint();
    void UpdateVisibility();
    void Tick();
    bool UpdateClockAndVolume();
    void SetHover(DockHit hover);
    const IconImage* IconFor(const DockApp& app);
    POINT Origin() const;
    void Click(DockHit hit);
    void ActivateApp(int index);
    void ShowMenu(int index);
    std::wstring LabelFor(DockHit hit) const;
    void ShowPreview(DockHit hit);
    void HidePreview();

    HINSTANCE instance_ = nullptr;
    HWND overlay_ = nullptr;
    Callbacks callbacks_;
    HWND window_ = nullptr;
    UINT shellHookMessage_ = 0;
    Canvas canvas_;
    DockModel model_;
    DockLayout layout_;
    DockState state_;
    std::map<std::wstring, IconImage> icons_;
    std::set<HWND> flashing_;
    HWND lastForeground_ = nullptr;
    bool enabled_ = false;
    bool allDesktops_ = false;
    bool haveSlot_ = false;
    RECT slot_ = {};
    float scale_ = 1.0f;
    int clockWidth_ = 0;
    std::vector<std::wstring> desktopNames_ = {L"Desktop 1"};
    bool tracking_ = false;
    DockHit pressed_;
    ULONGLONG hoverSince_ = 0;
    ULONGLONG awaySince_ = 0;
    ULONGLONG startTick_ = 0;
    ULONGLONG blockedUntil_ = 0;  // no preview right after a click
    MasterVolume volume_;

    // The preview popup.
    WindowPreview preview_;
    DockHit previewFor_;
    RECT placedRect_ = {};  // last reported through callbacks_.placed
    bool placedShown_ = false;
};

// Starts an app or opens a file the way Explorer would: through Explorer when we run as administrator, so the app
// doesn't inherit administrator rights.
void LaunchAsUser(const std::wstring& path);
