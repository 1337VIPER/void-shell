#pragma once

#include <array>
#include <functional>
#include <map>
#include <set>
#include <vector>

#include "canvas.h"
#include "common.h"

struct ZoneLayout {
    const wchar_t* name;
    std::vector<std::array<float, 4>> zones;  // left, top, right, bottom as fractions of the work area
};

const std::vector<ZoneLayout>& ZoneLayouts();
constexpr int kDefaultLayout = 3;  // Main and side
// Layouts are saved by index. Version 2 put "One window" first; older saved indices are moved up one on load.
constexpr int kLayoutVersion = 2;

float MonitorScale(HMONITOR monitor);
RECT WorkArea(HMONITOR monitor);

// Zone rectangles in screen pixels inside one work area, with even gaps between zones.
std::vector<RECT> ComputeZones(const ZoneLayout& layout, const RECT& work, float scale);
// The zone under a point, or the nearest one within a short reach; -1 if none.
int ZoneAt(const std::vector<RECT>& zones, POINT point, float scale);
// The drag preview: every zone as a glass panel, the hovered one lit. Zones are in canvas coordinates.
void DrawZonePreview(Canvas& canvas, const std::vector<RECT>& zones, int hovered, float scale);

// Watches window drags system-wide. While a window is being moved, the zones of the monitor under the mouse fade in;
// letting go snaps the window into the zone under the mouse. Holding Shift places it freely. Optionally places
// newly opened windows into the first free zone.
class ZoneSnapper {
public:
    bool Init(HINSTANCE instance);
    void Shutdown();
    void Configure(bool snap, bool autoPlace);
    // Which layout a window's zones follow (each virtual desktop can have its own).
    void SetLayoutResolver(std::function<int(HWND)> layoutFor) { layoutFor_ = std::move(layoutFor); }
    // Called when Windows refuses to move a window, which means it runs as administrator.
    void SetAccessDeniedHandler(std::function<void(HWND)> handler) { onAccessDenied_ = std::move(handler); }
    // Puts every snapped window back into its zone, after a layout or the work area changed.
    void Reflow();

private:
    struct Drag {
        HWND window = nullptr;
        bool resizing = false;
        bool moved = false;
        RECT start = {};
        HMONITOR monitor = nullptr;
        RECT work = {};
        float scale = 1.0f;
        std::vector<RECT> zones;
        int zone = -1;
        float fade = 0.0f;
        bool previewShown = false;
    };
    struct PendingSnap {
        HWND window;
        int zone;
        ULONGLONG due;
        bool verify;  // second pass: re-apply if the app resized itself afterwards
    };

    static void CALLBACK OnWinEvent(HWINEVENTHOOK hook, DWORD event, HWND window, LONG object, LONG child, DWORD thread,
                                    DWORD time);
    static void CALLBACK OnTick(HWND, UINT, UINT_PTR, DWORD);

    void Tick();
    void BeginDrag(HWND window);
    void UpdateDrag();
    void EndDrag(HWND window);
    void HidePreview();
    void Snap(HWND window, int zone);
    void OnShown(HWND window);
    void PlaceNewWindows();
    void EnsureTimer();
    const ZoneLayout& LayoutOf(HWND window) const;

    HWND preview_ = nullptr;
    Canvas canvas_;
    HWINEVENTHOOK moveHook_ = nullptr;
    HWINEVENTHOOK objectHook_ = nullptr;
    UINT_PTR timer_ = 0;
    std::function<int(HWND)> layoutFor_;
    std::function<void(HWND)> onAccessDenied_;
    bool snap_ = false;
    bool autoPlace_ = false;
    Drag drag_;
    std::map<HWND, int> assignments_;  // snapped window -> zone index
    std::set<HWND> known_;             // windows that existed or were already shown
    std::vector<std::pair<HWND, ULONGLONG>> newWindows_;
    std::vector<PendingSnap> pendingSnaps_;
};
