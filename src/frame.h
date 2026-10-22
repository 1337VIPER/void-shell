#pragma once

#include "common.h"  // windows.h must come before shellapi.h

#include <shellapi.h>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "bubbles.h"
#include "canvas.h"
#include "icons.h"
#include "rain.h"
#include "settings.h"
#include "sysinfo.h"
#include "zones.h"

// How the frame looks. Slim: a thin glowing border. Wide: a broad margin around the desktop with code raining
// through it from every edge toward the desktop.
struct FrameStyle {
    bool wide = false;
    int margin = 1;  // wide only: 0 small, 1 medium, 2 large
    bool rain = true;
    RainColor rainColor = RainColor::Venom;
    int dockBand = 0;  // the main monitor's bottom band is at least this tall (pixels at scale 1) to hold the dock
};

// One monitor's frame, in physical screen pixels.
struct FrameGeometry {
    RECT monitor;
    RECT inner;  // the opening the desktop shows through: the work area inside the frame
    int top;     // band thicknesses; the top band ends in a strip with the brand, desktop name and buttons
    int side;
    int bottom;
    int strip;
    int radius;  // rounded corners of the opening
    float scale;
    bool wide;
};

enum class FrameButton { None, Void, Settings, Layout, Stats, Add };

struct TopBarContent {
    std::wstring brand;
    std::wstring desktop;
    const ZoneLayout* layout;
};

// Band sizes for a monitor at a display scale (inner is left for the caller to fill).
FrameGeometry FrameSizes(const RECT& monitor, float scale, const FrameStyle& style);
// Bar window rectangles for a finished geometry: top, left, right, bottom. The top and bottom windows reach
// `radius` past their band so they can draw the opening's rounded corners; that overhang is transparent except
// in the corners, and transparent pixels pass clicks through.
void FrameBarRects(const FrameGeometry& geometry, RECT rects[4]);
// Where code rains inside a bar (bar-local), and which way. False for the slim style.
bool FrameRainArea(const FrameGeometry& geometry, const RECT& bar, int index, RECT& area, RainFlow& flow);
// Paints one bar's still image (barRect in screen coordinates); the top bar also gets its strip.
void PaintFrameBar(Canvas& canvas, const FrameGeometry& geometry, const RECT& barRect, bool topBar,
                   const TopBarContent& content, FrameButton hover);
// Which top-bar button is at a point in the top bar window's coordinates.
FrameButton FrameButtonAt(const FrameGeometry& geometry, int barWidth, POINT local);

// The "outer shell": a frame around each monitor. Each side is an appbar, so Windows keeps maximized and snapped
// windows inside it. The top strip carries the Venom mark and brand, the current desktop's name, a gear for
// settings and a button for the zone layout. In the wide style, matrix code rains through the margin.
class ShellFrame {
public:
    struct Callbacks {
        std::function<void()> openVoid;
        std::function<void()> openSettings;
        std::function<void(POINT)> chooseLayout;  // screen point just under the layout button
        std::function<void()> workAreaChanged;
        std::function<void()> barsChanged;  // moved, shown or hidden: whatever sits on the frame should follow
        std::function<void()> openStats;    // the system orbs were clicked
        std::function<void(POINT)> addShortcut;  // the + button; screen point just under it
        std::function<void(int)> openShortcut;   // a bubble was clicked (index into the shortcuts)
        std::function<void(int, POINT)> shortcutMenu;  // a bubble was right-clicked, at a screen point
        std::function<void(const std::vector<Shortcut>&)> shortcutsChanged;  // added, moved or removed: save them
        std::function<void(int)> hoverShortcut;   // the pointer moved onto a bubble (its index) or off them all (-1)
        std::function<void()> bubblePressed;      // a bubble was pressed (a click or the start of a drag)
        std::function<void(int)> launchShortcut;  // a bubble was middle-clicked: start another one
    };

    bool Init(HINSTANCE instance, HWND voidOverlay, Callbacks callbacks);
    void SetEnabled(bool enabled);
    bool Enabled() const { return enabled_; }
    void SetStyle(const FrameStyle& style);
    // Every monitor, or the main one only.
    void SetAllMonitors(bool all);
    // Hides the frame on one monitor (a fullscreen game is there); null shows it everywhere again.
    void SetSuppressed(HMONITOR monitor);
    // Whether the rain may move at all (not while locked, on battery or while the void is open).
    void SetAnimationAllowed(bool allowed);
    void SetBrand(const std::wstring& brand);
    void SetDesktopName(const std::wstring& name);
    void SetLayout(int index);
    // CPU, GPU and memory orbs at the top of each frame.
    void SetStatsEnabled(bool enabled);
    void SetStats(const SystemStats& stats);
    // Folder and app bubbles in the wide margin. Apps sit in the side bands, folders in the top and bottom bands;
    // each can be dragged anywhere in the bands of its kind, on any monitor with the frame.
    void SetShortcuts(const std::vector<Shortcut>& shortcuts);
    const std::vector<Shortcut>& Shortcuts() const { return shortcuts_; }
    // Places a new one in the first free spot on the monitor at `at` (a screen point) and reports the change. False if that
    // target is already there.
    bool AddShortcut(Shortcut shortcut, POINT at);
    void RemoveShortcut(int index);
    // The dock's rectangle on screen (null when hidden), so bubbles keep clear of it.
    void SetDockArea(const RECT* area);
    // Each shortcut's app state (lined up with Shortcuts()), shown under app bubbles as the taskbar does.
    void SetShortcutStates(const std::vector<BubbleState>& states);
    // Empty sockets always showing, or only while a bubble is dragged.
    void SetShowSockets(bool show);
    // A shown bubble's round part on screen, the band it's in, and its monitor; for placing a preview beside it.
    bool ShortcutAnchor(int index, RECT& bubble, int& band, RECT& monitor, float& scale);
    // Monitors, display scale or Explorer changed: build everything again shortly.
    void ScheduleRebuild();
    // The main monitor's bottom band in screen pixels, while it is showing, and that monitor's display scale.
    bool DockSlot(RECT& slot, float& scale) const;
    void Shutdown();
    // Gives the reserved screen space back to Windows. Safe to call from a crash handler.
    void RemoveAppBars();

private:
    struct Bar {
        HWND window = nullptr;
        UINT edge = ABE_TOP;
        RECT reserved = {};
        RECT rect = {};
        Canvas still;   // everything but the rain, repainted only when something changes
        Canvas canvas;  // what is shown: the still image plus the rain
        RainField rain;
    };
    struct Bubble {
        int index = -1;  // into shortcuts_
        int socket = -1;  // into its frame's sockets for that bar
        RECT cell = {};  // bar-local
        Canvas sprite;
    };
    struct MonitorFrame {
        HMONITOR monitor = nullptr;
        std::wstring device;  // "\\.\DISPLAY1", which shortcuts remember their monitor by
        std::vector<std::unique_ptr<Bubble>> bubbles[4];  // per bar
        std::vector<RECT> sockets[4];                      // per bar, bar-local
        Canvas socketSprites[2][3];                        // [app, folder][SocketLook]
        FrameGeometry geometry = {};
        Bar bars[4];  // top, left, right, bottom
        RainGlyphs glyphs;
        FrameButton hover = FrameButton::None;
        Canvas orbs;      // the system orbs, drawn over the top bar's rain
        RECT orbsBox = {};  // where, in the top bar
        bool primary = false;
        bool hidden = false;  // a fullscreen app is covering this monitor
    };

    static LRESULT CALLBACK BarProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    static void CALLBACK OnTimer(HWND, UINT, UINT_PTR id, DWORD);
    static BOOL CALLBACK OnMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM self);

    LRESULT HandleBar(MonitorFrame& frame, int bar, HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    MonitorFrame* FrameOf(HWND window, int* bar);
    void Build();
    void Destroy();
    void AddMonitor(HMONITOR monitor);
    bool Reserve(MonitorFrame& frame);
    void LayoutAll();
    void Paint(MonitorFrame& frame, int bar);    // the still image, then Compose
    void Compose(MonitorFrame& frame, int bar);  // still image + rain, shown
    void ComposeAll();
    void Animate();
    void UpdateAnimationTimer();
    bool Raining(const MonitorFrame& frame) const;
    void ShowBars(MonitorFrame& frame, bool show);
    bool ShouldShow(const MonitorFrame& frame) const { return !frame.hidden && frame.monitor != suppressed_; }
    void OnFullscreen(MonitorFrame& frame, bool opened);
    void ScheduleLayout();
    TopBarContent Content() const;
    void NotifyBarsChanged();
    void PaintOrbsOverlay(MonitorFrame& frame);
    // Bubbles.
    void LayoutBubbles();
    MonitorFrame* FrameFor(const Shortcut& shortcut);
    std::vector<RECT> Reserved(const MonitorFrame& frame, int bar) const;
    const IconImage* BubbleIcon(const Shortcut& shortcut, int size);
    Bubble* BubbleAt(MonitorFrame& frame, int bar, POINT local);
    bool FindBubble(int index, MonitorFrame** frame, int* bar, Bubble** bubble);
    void PaintBubbleSprite(MonitorFrame& frame, Bubble& bubble);
    void SetBubbleHover(int index);
    void ShowBubbleLabel(int index);
    bool DropTarget(POINT centre, MonitorFrame** frame, int* bar, int* socket);
    void ComposeEverything();
    void StartDrag();
    void MoveDrag(POINT screen);
    void Drop(POINT screen);
    void CancelDrag();
    void NotifyShortcuts();

    HINSTANCE instance_ = nullptr;
    HWND overlay_ = nullptr;
    Callbacks callbacks_;
    bool enabled_ = false;
    bool allMonitors_ = true;
    bool animationAllowed_ = true;
    FrameStyle style_;
    HMONITOR suppressed_ = nullptr;
    std::vector<std::unique_ptr<MonitorFrame>> monitors_;
    std::wstring brand_ = L"VenomMind";
    std::wstring desktopName_ = L"Desktop 1";
    int layout_ = kDefaultLayout;
    bool statsEnabled_ = false;
    SystemStats stats_;
    std::vector<Shortcut> shortcuts_;
    std::map<std::wstring, IconImage> bubbleIcons_;
    RECT dockArea_ = {};
    bool haveDockArea_ = false;
    int hoverShortcut_ = -1;
    int pressed_ = -1;      // a bubble the left button went down on
    POINT pressAt_ = {};    // where, on screen
    POINT grab_ = {};       // where in its cell
    bool dragging_ = false;
    ShortcutKind dragKind_ = ShortcutKind::App;
    std::wstring dragFromDevice_;  // where the dragged bubble came from, for a bubble it swaps places with
    int dragFromBar_ = -1;
    float dragFromFx_ = 0.0f;
    float dragFromFy_ = 0.0f;
    MonitorFrame* dropFrame_ = nullptr;  // the socket it would land in now
    int dropBar_ = -1;
    int dropSocket_ = -1;
    std::vector<BubbleState> states_;
    bool showSockets_ = true;
    HWND ghost_ = nullptr;  // the bubble following the pointer while dragged
    Canvas ghostCanvas_;
    HWND label_ = nullptr;  // a bubble's name on hover, when the margin is too small to show it underneath
    Canvas labelCanvas_;
    UINT_PTR layoutTimer_ = 0;
    UINT_PTR rebuildTimer_ = 0;
    UINT_PTR animationTimer_ = 0;
    LARGE_INTEGER lastFrame_ = {};
};
