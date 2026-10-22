#include "zones.h"

#include <dwmapi.h>
#include <shellscalingapi.h>

#include <algorithm>
#include <cmath>
#include <cwchar>

namespace {

constexpr wchar_t kPreviewClass[] = L"VoidShellZonePreview";
constexpr ULONGLONG kSnapDelayMs = 40;      // let Windows finish its own drop handling first
constexpr ULONGLONG kVerifyDelayMs = 160;   // then check the app didn't resize itself back
constexpr ULONGLONG kNewWindowDelayMs = 250;  // apps often position themselves right after showing

ZoneSnapper* g_snapper = nullptr;

bool IsCloaked(HWND window) {
    DWORD cloaked = 0;
    return SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked != 0;
}

// Ordinary resizable app windows: not dialogs, tool windows, the desktop or the taskbar.
bool IsSnappable(HWND window) {
    if (!IsWindow(window) || !IsWindowVisible(window) || IsIconic(window) || IsCloaked(window)) return false;
    if (GetAncestor(window, GA_ROOT) != window || GetWindow(window, GW_OWNER)) return false;
    const LONG style = GetWindowLongW(window, GWL_STYLE);
    const LONG exStyle = GetWindowLongW(window, GWL_EXSTYLE);
    if (!(style & WS_THICKFRAME) || (style & WS_CHILD)) return false;
    if (exStyle & (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE)) return false;
    wchar_t className[64] = {};
    GetClassNameW(window, className, ARRAYSIZE(className));
    for (const wchar_t* shell : {L"Progman", L"WorkerW", L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd"})
        if (wcscmp(className, shell) == 0) return false;
    return true;
}

bool CoversMonitor(HWND window) {
    MONITORINFO info = {sizeof(info)};
    RECT rect;
    if (!GetWindowRect(window, &rect) || !GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &info))
        return false;
    return rect.left <= info.rcMonitor.left && rect.top <= info.rcMonitor.top && rect.right >= info.rcMonitor.right &&
           rect.bottom >= info.rcMonitor.bottom;
}

// The visible frame (without the invisible resize borders Windows 10/11 adds around most windows).
RECT VisibleFrame(HWND window) {
    RECT frame;
    if (FAILED(DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS, &frame, sizeof(frame))))
        GetWindowRect(window, &frame);
    return frame;
}

// Returns 0 on success, otherwise the Windows error (ERROR_ACCESS_DENIED for apps running as administrator).
DWORD MoveVisibleFrameTo(HWND window, const RECT& target) {
    if (IsZoomed(window)) ShowWindow(window, SW_RESTORE);
    RECT outer;
    GetWindowRect(window, &outer);
    const RECT frame = VisibleFrame(window);
    const int left = frame.left - outer.left;
    const int top = frame.top - outer.top;
    const int right = outer.right - frame.right;
    const int bottom = outer.bottom - frame.bottom;
    if (SetWindowPos(window, nullptr, target.left - left, target.top - top, (target.right - target.left) + left + right,
                     (target.bottom - target.top) + top + bottom, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER))
        return 0;
    const DWORD error = GetLastError();
    return error ? error : ERROR_ACCESS_DENIED;
}

}  // namespace

const std::vector<ZoneLayout>& ZoneLayouts() {
    static const std::vector<ZoneLayout> layouts = {
        {L"One window", {{0.0f, 0.0f, 1.0f, 1.0f}}},  // a single panel: every window fills the frame's opening
        {L"Two columns", {{0.0f, 0.0f, 0.5f, 1.0f}, {0.5f, 0.0f, 1.0f, 1.0f}}},
        {L"Three columns", {{0.0f, 0.0f, 1.0f / 3, 1.0f}, {1.0f / 3, 0.0f, 2.0f / 3, 1.0f}, {2.0f / 3, 0.0f, 1.0f, 1.0f}}},
        {L"Main and side", {{0.0f, 0.0f, 0.62f, 1.0f}, {0.62f, 0.0f, 1.0f, 0.5f}, {0.62f, 0.5f, 1.0f, 1.0f}}},
        {L"Grid", {{0.0f, 0.0f, 0.5f, 0.5f}, {0.5f, 0.0f, 1.0f, 0.5f}, {0.0f, 0.5f, 0.5f, 1.0f}, {0.5f, 0.5f, 1.0f, 1.0f}}},
        {L"Wide centre", {{0.22f, 0.0f, 0.78f, 1.0f}, {0.0f, 0.0f, 0.22f, 1.0f}, {0.78f, 0.0f, 1.0f, 1.0f}}},
    };
    return layouts;
}

float MonitorScale(HMONITOR monitor) {
    UINT dpiX = 96;
    UINT dpiY = 96;
    if (FAILED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY))) return 1.0f;
    return float(dpiX) / 96.0f;
}

RECT WorkArea(HMONITOR monitor) {
    MONITORINFO info = {sizeof(info)};
    GetMonitorInfoW(monitor, &info);
    return info.rcWork;
}

std::vector<RECT> ComputeZones(const ZoneLayout& layout, const RECT& work, float scale) {
    const int gap = int(lroundf(10.0f * scale));
    const int margin = int(lroundf(6.0f * scale));
    const float left = float(work.left + margin);
    const float top = float(work.top + margin);
    const float width = float(work.right - work.left - margin * 2);
    const float height = float(work.bottom - work.top - margin * 2);
    std::vector<RECT> zones;
    for (const auto& f : layout.zones) {
        // Edges shared with another zone give up half a gap each; edges on the border keep the margin.
        RECT zone;
        zone.left = LONG(lroundf(left + f[0] * width)) + (f[0] > 0.0f ? gap / 2 : 0);
        zone.top = LONG(lroundf(top + f[1] * height)) + (f[1] > 0.0f ? gap / 2 : 0);
        zone.right = LONG(lroundf(left + f[2] * width)) - (f[2] < 1.0f ? gap - gap / 2 : 0);
        zone.bottom = LONG(lroundf(top + f[3] * height)) - (f[3] < 1.0f ? gap - gap / 2 : 0);
        zones.push_back(zone);
    }
    return zones;
}

int ZoneAt(const std::vector<RECT>& zones, POINT point, float scale) {
    int best = -1;
    long bestDistance = long(lroundf(48.0f * scale));
    for (size_t i = 0; i < zones.size(); ++i) {
        const RECT& zone = zones[i];
        const long dx = std::max({zone.left - point.x, 0L, point.x - zone.right + 1});
        const long dy = std::max({zone.top - point.y, 0L, point.y - zone.bottom + 1});
        const long distance = std::max(dx, dy);
        if (distance == 0) return int(i);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = int(i);
        }
    }
    return best;
}

void DrawZonePreview(Canvas& canvas, const std::vector<RECT>& zones, int hovered, float scale) {
    canvas.Clear();
    const float radius = 10.0f * scale;
    for (size_t i = 0; i < zones.size(); ++i) {
        const RECT& z = zones[i];
        const float l = float(z.left);
        const float t = float(z.top);
        const float r = float(z.right);
        const float b = float(z.bottom);
        if (int(i) == hovered) {
            canvas.GlowRoundRect(l, t, r, b, radius, 9.0f * scale, {0.35f, 0.60f, 1.0f, 0.40f});
            canvas.FillRoundRect(l, t, r, b, radius, {0.30f, 0.55f, 1.0f, 0.26f});
            canvas.StrokeRoundRect(l, t, r, b, radius, 2.0f * scale, {0.72f, 0.88f, 1.0f, 0.95f});
        } else {
            canvas.FillRoundRect(l, t, r, b, radius, {0.25f, 0.42f, 1.0f, 0.10f});
            canvas.StrokeRoundRect(l, t, r, b, radius, 1.5f * scale, {0.55f, 0.70f, 1.0f, 0.55f});
        }
    }
}

bool ZoneSnapper::Init(HINSTANCE instance) {
    g_snapper = this;
    WNDCLASSEXW windowClass = {sizeof(windowClass)};
    windowClass.lpfnWndProc = DefWindowProcW;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = kPreviewClass;
    RegisterClassExW(&windowClass);
    // Click-through and never activated: it only shows where a window will land.
    preview_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                               kPreviewClass, L"Void Shell zones", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, instance,
                               nullptr);
    return preview_ != nullptr;
}

void ZoneSnapper::Shutdown() {
    Configure(false, false);
    if (timer_) KillTimer(nullptr, timer_);
    timer_ = 0;
    if (preview_) DestroyWindow(preview_);
    preview_ = nullptr;
    g_snapper = nullptr;
}

void ZoneSnapper::Configure(bool snap, bool autoPlace) {
    const bool wanted = snap || autoPlace;
    if (wanted && !moveHook_) {
        const DWORD flags = WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS;
        moveHook_ = SetWinEventHook(EVENT_SYSTEM_MOVESIZESTART, EVENT_SYSTEM_MOVESIZEEND, nullptr, OnWinEvent, 0, 0, flags);
        objectHook_ = SetWinEventHook(EVENT_OBJECT_DESTROY, EVENT_OBJECT_SHOW, nullptr, OnWinEvent, 0, 0, flags);
        if (!moveHook_ || !objectHook_) Log("Could not watch window moves (SetWinEventHook failed)");
    } else if (!wanted && moveHook_) {
        UnhookWinEvent(moveHook_);
        if (objectHook_) UnhookWinEvent(objectHook_);
        moveHook_ = objectHook_ = nullptr;
        HidePreview();
        drag_ = Drag();
    }
    if (autoPlace && !autoPlace_) {
        // Only windows that open from now on are placed; everything already open is left alone.
        known_.clear();
        EnumWindows([](HWND window, LPARAM known) -> BOOL {
            reinterpret_cast<std::set<HWND>*>(known)->insert(window);
            return TRUE;
        }, reinterpret_cast<LPARAM>(&known_));
    }
    snap_ = snap;
    autoPlace_ = autoPlace;
}

const ZoneLayout& ZoneSnapper::LayoutOf(HWND window) const {
    const int index = layoutFor_ ? layoutFor_(window) : kDefaultLayout;
    return ZoneLayouts()[std::clamp(index, 0, int(ZoneLayouts().size()) - 1)];
}

void ZoneSnapper::Reflow() {
    // Moving another app's window can deliver window events mid-loop, so work from a copy.
    const std::vector<std::pair<HWND, int>> snapped(assignments_.begin(), assignments_.end());
    for (const auto& [window, zone] : snapped) {
        if (!IsWindow(window)) {
            assignments_.erase(window);
        } else if (IsSnappable(window)) {
            Snap(window, zone);
        }
    }
}

void CALLBACK ZoneSnapper::OnWinEvent(HWINEVENTHOOK, DWORD event, HWND window, LONG object, LONG child, DWORD,
                                      DWORD) {
    ZoneSnapper* self = g_snapper;
    if (!self || !window || object != OBJID_WINDOW || child != CHILDID_SELF) return;
    switch (event) {
        case EVENT_SYSTEM_MOVESIZESTART:
            if (self->snap_) self->BeginDrag(window);
            break;
        case EVENT_SYSTEM_MOVESIZEEND:
            if (self->snap_) self->EndDrag(window);
            break;
        case EVENT_OBJECT_SHOW:
            self->OnShown(window);
            break;
        case EVENT_OBJECT_DESTROY:
            self->assignments_.erase(window);
            self->known_.erase(window);
            break;
    }
}

void CALLBACK ZoneSnapper::OnTick(HWND, UINT, UINT_PTR, DWORD) {
    if (g_snapper) g_snapper->Tick();
}

void ZoneSnapper::EnsureTimer() {
    if (!timer_) timer_ = SetTimer(nullptr, 0, 16, OnTick);
}

void ZoneSnapper::Tick() {
    UpdateDrag();
    PlaceNewWindows();

    const ULONGLONG now = GetTickCount64();
    std::vector<PendingSnap> due;
    for (auto it = pendingSnaps_.begin(); it != pendingSnaps_.end();) {
        if (it->due <= now) {
            due.push_back(*it);
            it = pendingSnaps_.erase(it);
        } else {
            ++it;
        }
    }
    for (const PendingSnap& pending : due) {
        if (!IsSnappable(pending.window)) continue;
        if (pending.verify) {
            // Some apps resize themselves right after a move (often when crossing to a monitor with another DPI).
            const HMONITOR monitor = MonitorFromWindow(pending.window, MONITOR_DEFAULTTONEAREST);
            const std::vector<RECT> zones =
                ComputeZones(LayoutOf(pending.window), WorkArea(monitor), MonitorScale(monitor));
            const RECT frame = VisibleFrame(pending.window);
            const RECT& zone = zones[std::min(pending.zone, int(zones.size()) - 1)];
            if (abs(frame.left - zone.left) > 2 || abs(frame.top - zone.top) > 2 || abs(frame.right - zone.right) > 2 ||
                abs(frame.bottom - zone.bottom) > 2) {
                Snap(pending.window, pending.zone);
            }
        } else {
            Snap(pending.window, pending.zone);
            pendingSnaps_.push_back({pending.window, pending.zone, now + kVerifyDelayMs, true});
        }
    }

    if (!drag_.window && newWindows_.empty() && pendingSnaps_.empty() && timer_) {
        KillTimer(nullptr, timer_);
        timer_ = 0;
    }
}

void ZoneSnapper::BeginDrag(HWND window) {
    if (!IsSnappable(window)) return;
    drag_ = Drag();
    drag_.window = window;
    GetWindowRect(window, &drag_.start);
    // Moving or resizing? Ask the window what is under the mouse where the drag started.
    POINT cursor;
    GetCursorPos(&cursor);
    DWORD_PTR hit = HTCAPTION;
    SendMessageTimeoutW(window, WM_NCHITTEST, 0, MAKELPARAM(cursor.x, cursor.y), SMTO_ABORTIFHUNG, 50, &hit);
    drag_.resizing = hit >= HTLEFT && hit <= HTBOTTOMRIGHT;
    EnsureTimer();
}

void ZoneSnapper::UpdateDrag() {
    if (!drag_.window || drag_.resizing) return;
    if (!IsWindow(drag_.window)) {
        HidePreview();
        drag_ = Drag();
        return;
    }
    if (!drag_.moved) {
        RECT now;
        GetWindowRect(drag_.window, &now);
        if (now.left == drag_.start.left && now.top == drag_.start.top) return;
        drag_.moved = true;
    }
    if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {  // free placement
        HidePreview();
        drag_.zone = -1;
        return;
    }

    POINT cursor;
    GetCursorPos(&cursor);
    const HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    bool redraw = false;
    if (monitor != drag_.monitor) {
        drag_.monitor = monitor;
        drag_.work = WorkArea(monitor);
        drag_.scale = MonitorScale(monitor);
        drag_.zones = ComputeZones(LayoutOf(drag_.window), drag_.work, drag_.scale);
        canvas_.Resize(drag_.work.right - drag_.work.left, drag_.work.bottom - drag_.work.top);
        drag_.zone = -2;
        drag_.fade = 0.0f;
        redraw = true;
    }
    const int zone = ZoneAt(drag_.zones, cursor, drag_.scale);
    if (zone != drag_.zone) {
        drag_.zone = zone;
        redraw = true;
    }
    if (redraw) {
        std::vector<RECT> local = drag_.zones;
        for (RECT& r : local) OffsetRect(&r, -drag_.work.left, -drag_.work.top);
        DrawZonePreview(canvas_, local, drag_.zone, drag_.scale);
    }
    if (redraw || drag_.fade < 1.0f || !drag_.previewShown) {
        drag_.fade = std::min(1.0f, drag_.fade + 0.18f);
        canvas_.Present(preview_, {drag_.work.left, drag_.work.top}, BYTE(255.0f * drag_.fade));
        if (!drag_.previewShown) {
            SetWindowPos(preview_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
            drag_.previewShown = true;
        }
    }
}

void ZoneSnapper::EndDrag(HWND window) {
    if (window != drag_.window) return;
    UpdateDrag();
    HidePreview();
    const bool free = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    if (!drag_.resizing && drag_.moved) {
        if (!free && drag_.zone >= 0) {
            pendingSnaps_.push_back({window, drag_.zone, GetTickCount64() + kSnapDelayMs, false});
            EnsureTimer();
        } else {
            assignments_.erase(window);  // placed freely
        }
    }
    drag_ = Drag();
}

void ZoneSnapper::HidePreview() {
    if (drag_.previewShown && preview_) ShowWindow(preview_, SW_HIDE);
    drag_.previewShown = false;
    drag_.fade = 0.0f;
    drag_.monitor = nullptr;
}

void ZoneSnapper::Snap(HWND window, int zone) {
    const HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
    const std::vector<RECT> zones = ComputeZones(LayoutOf(window), WorkArea(monitor), MonitorScale(monitor));
    if (zones.empty()) return;
    zone = zone % int(zones.size());
    if (const DWORD error = MoveVisibleFrameTo(window, zones[zone])) {
        Log("Couldn't move a window into its zone (error %lu)", error);
        if (error == ERROR_ACCESS_DENIED && onAccessDenied_) onAccessDenied_(window);
        return;
    }
    assignments_[window] = zone;
}

void ZoneSnapper::OnShown(HWND window) {
    if (!autoPlace_ || GetAncestor(window, GA_ROOT) != window) return;
    if (!known_.insert(window).second) return;  // shown before, e.g. restored from the tray
    newWindows_.push_back({window, GetTickCount64() + kNewWindowDelayMs});
    EnsureTimer();
}

void ZoneSnapper::PlaceNewWindows() {
    const ULONGLONG now = GetTickCount64();
    std::vector<HWND> due;
    for (auto it = newWindows_.begin(); it != newWindows_.end();) {
        if (it->second <= now) {
            due.push_back(it->first);
            it = newWindows_.erase(it);
        } else {
            ++it;
        }
    }
    for (const HWND window : due) {
        // Leave dialogs, maximized and fullscreen windows where the app put them.
        if (!autoPlace_ || !IsSnappable(window) || IsZoomed(window) || CoversMonitor(window)) continue;

        const HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
        const std::vector<RECT> zones = ComputeZones(LayoutOf(window), WorkArea(monitor), MonitorScale(monitor));
        std::vector<bool> taken(zones.size(), false);
        for (const auto& [other, zone] : assignments_) {
            if (other != window && IsWindowVisible(other) && !IsIconic(other) &&
                MonitorFromWindow(other, MONITOR_DEFAULTTONEAREST) == monitor && zone < int(taken.size())) {
                taken[zone] = true;
            }
        }
        int zone = int(std::find(taken.begin(), taken.end(), false) - taken.begin());
        if (zone >= int(zones.size())) {
            // Every zone is in use: take the one the window mostly sits in.
            const RECT frame = VisibleFrame(window);
            zone = std::max(0, ZoneAt(zones, {(frame.left + frame.right) / 2, (frame.top + frame.bottom) / 2}, 100.0f));
        }
        Snap(window, zone);
    }
}
