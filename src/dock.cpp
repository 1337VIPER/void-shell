#include "dock.h"

#include <exdisp.h>
#include <shellapi.h>
#include <shldisp.h>
#include <shlobj.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>

#include "brand.h"
#include "startup.h"
#include "taskbar.h"

namespace {

constexpr wchar_t kDockClass[] = L"VenomMindDock";
constexpr UINT_PTR kRefreshTimer = 1;
constexpr UINT_PTR kTickTimer = 2;
constexpr UINT_PTR kPeriodicTimer = 3;  // catches anything the shell hook missed
constexpr UINT kTickMs = 100;
constexpr UINT kPeriodicMs = 2000;
constexpr ULONGLONG kPreviewDelay = 450;
constexpr ULONGLONG kLabelDelay = 700;
constexpr ULONGLONG kHideDelay = 350;
constexpr ULONGLONG kAfterClick = 900;
constexpr UINT kMenuOpen = 1;
constexpr UINT kMenuPin = 2;
constexpr UINT kMenuUnpin = 3;
constexpr UINT kMenuClose = 4;
constexpr UINT kMenuTaskManager = 5;
constexpr UINT kMenuSettings = 6;

Dock* g_dock = nullptr;

// Raymond Chen's way to run something as the signed-in user from an elevated process: ask the desktop's Explorer
// window to ShellExecute it.
bool ShellExecuteThroughExplorer(const std::wstring& path) {
    ComPtr<IShellWindows> windows;
    if (FAILED(CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&windows)))) return false;
    VARIANT location;
    VariantInit(&location);
    location.vt = VT_I4;
    location.lVal = CSIDL_DESKTOP;
    VARIANT root;
    VariantInit(&root);
    long found = 0;
    ComPtr<IDispatch> dispatch;
    if (FAILED(windows->FindWindowSW(&location, &root, SWC_DESKTOP, &found, SWFO_NEEDDISPATCH, &dispatch)) || !dispatch)
        return false;
    ComPtr<IServiceProvider> provider;
    ComPtr<IShellBrowser> browser;
    ComPtr<IShellView> view;
    ComPtr<IDispatch> background;
    ComPtr<IShellFolderViewDual> folderView;
    ComPtr<IDispatch> application;
    ComPtr<IShellDispatch2> shell;
    if (FAILED(dispatch.As(&provider)) ||
        FAILED(provider->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(&browser))) ||
        FAILED(browser->QueryActiveShellView(&view)) ||
        FAILED(view->GetItemObject(SVGIO_BACKGROUND, IID_PPV_ARGS(&background))) || FAILED(background.As(&folderView)) ||
        FAILED(folderView->get_Application(&application)) || FAILED(application.As(&shell)))
        return false;
    BSTR file = SysAllocString(path.c_str());
    VARIANT none;
    VariantInit(&none);
    VARIANT show;
    VariantInit(&show);
    show.vt = VT_I4;
    show.lVal = SW_SHOWNORMAL;
    const HRESULT hr = shell->ShellExecute(file, none, none, none, show);
    SysFreeString(file);
    return SUCCEEDED(hr);
}

void OpenSettingsPage(const wchar_t* uri) { ShellExecuteW(nullptr, nullptr, uri, nullptr, nullptr, SW_SHOWNORMAL); }

std::wstring OwnExe() {
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring text = path;
    CharLowerBuffW(text.data(), DWORD(text.size()));
    return text;
}

}  // namespace

void LaunchAsUser(const std::wstring& path) {
    if (path.empty()) return;
    AllowSetForegroundWindow(ASFW_ANY);  // so what starts can come to the front
    if (IsElevated()) {
        // Never fall back to starting it ourselves: it would run as administrator without anyone asking for that.
        if (!ShellExecuteThroughExplorer(path)) Log("Could not start an app through Explorer");
        return;
    }
    ShellExecuteW(nullptr, nullptr, path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

bool Dock::Init(HINSTANCE instance, HWND voidOverlay, Callbacks callbacks) {
    g_dock = this;
    instance_ = instance;
    overlay_ = voidOverlay;
    callbacks_ = std::move(callbacks);
    startTick_ = GetTickCount64();

    WNDCLASSEXW dockClass = {sizeof(dockClass)};
    dockClass.lpfnWndProc = DockProc;
    dockClass.hInstance = instance;
    dockClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    dockClass.lpszClassName = kDockClass;
    RegisterClassExW(&dockClass);

    // Like the frame: never activated, so clicking the dock leaves your app focused.
    window_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, kDockClass,
                              L"VenomMind dock", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, instance, nullptr);
    WindowPreview::Callbacks previewCallbacks;
    previewCallbacks.picked = [this] {
        blockedUntil_ = GetTickCount64() + kAfterClick;
        ScheduleRefresh(150);
    };
    previewCallbacks.closed = [this] { ScheduleRefresh(400); };  // Refresh updates the preview
    if (!window_ || !preview_.Init(instance, previewCallbacks)) return false;
    shellHookMessage_ = RegisterWindowMessageW(L"SHELLHOOK");
    return true;
}

void Dock::SetPins(const std::vector<std::wstring>& pins) {
    model_.SetPins(pins);
    if (Visible()) Refresh();
}

void Dock::SetEnabled(bool enabled) {
    if (enabled == enabled_) return;
    enabled_ = enabled;
    // The shell tells this window when windows open, close, take the focus or flash for attention.
    if (enabled) {
        RegisterShellHookWindow(window_);
    } else {
        DeregisterShellHookWindow(window_);
    }
    UpdateVisibility();
}

void Dock::SetAllDesktops(bool all) {
    if (all == allDesktops_) return;
    allDesktops_ = all;
    ScheduleRefresh(0);
}

void Dock::SetSlot(const RECT* slot, float scale) {
    if (slot) {
        if (scale != scale_ || clockWidth_ == 0) {
            scale_ = scale;
            icons_.clear();  // icons are kept at the pixel size they're drawn at
            clockWidth_ = DockClockWidth(scale);
        }
        const bool same = haveSlot_ && EqualRect(&slot_, slot);
        slot_ = *slot;
        haveSlot_ = true;
        if (same && Visible()) {
            BringToTop();
            return;
        }
    } else {
        haveSlot_ = false;
    }
    UpdateVisibility();
}

void Dock::SetDesktops(const std::vector<std::wstring>& names, int current) {
    const std::vector<std::wstring> list = names.empty() ? std::vector<std::wstring>{L"Desktop 1"} : names;
    const bool countChanged = list.size() != desktopNames_.size();
    const bool switched = current != state_.currentDesktop;
    desktopNames_ = list;
    state_.currentDesktop = current;
    if (!Visible()) return;
    if (countChanged) Relayout();
    if (countChanged || switched) Repaint();
    if (switched && !allDesktops_) ScheduleRefresh(150);  // another desktop, other windows
}

void Dock::BringToTop() {
    if (!Visible()) return;
    // While the void is open, stay just under it.
    const HWND after = IsWindowVisible(overlay_) ? overlay_ : HWND_TOPMOST;
    SetWindowPos(window_, after, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void Dock::Shutdown() {
    if (window_) {
        SetEnabled(false);
        DestroyWindow(window_);
    }
    preview_.Shutdown();
    window_ = nullptr;
    volume_ = MasterVolume();  // let go of the audio endpoint while COM is still up
    g_dock = nullptr;
}

void Dock::UpdateVisibility() {
    if (!window_) return;
    if (Visible()) {
        SetTimer(window_, kTickTimer, kTickMs, nullptr);
        SetTimer(window_, kPeriodicTimer, kPeriodicMs, nullptr);
        UpdateClockAndVolume();
        Refresh();
        BringToTop();
    } else {
        KillTimer(window_, kTickTimer);
        KillTimer(window_, kPeriodicTimer);
        KillTimer(window_, kRefreshTimer);
        HidePreview();
        ShowWindow(window_, SW_HIDE);
        if (placedShown_ && callbacks_.placed) callbacks_.placed(nullptr);
        placedShown_ = false;
    }
}

void Dock::ScheduleRefresh(UINT delay) {
    if (Visible()) SetTimer(window_, kRefreshTimer, std::max(1u, delay), nullptr);
}

void Dock::OnShellEvent(WPARAM code, HWND window) {
    switch (code) {
        case HSHELL_FLASH:
            flashing_.insert(window);
            ScheduleRefresh(30);
            break;
        case HSHELL_WINDOWACTIVATED:
        case HSHELL_RUDEAPPACTIVATED:
            flashing_.erase(window);
            ScheduleRefresh(30);
            break;
        case HSHELL_WINDOWDESTROYED:
            flashing_.erase(window);
            ScheduleRefresh();
            break;
        case HSHELL_WINDOWCREATED:
        case HSHELL_WINDOWREPLACED:
        case HSHELL_REDRAW:  // a title changed
        case HSHELL_MONITORCHANGED:
            ScheduleRefresh();
            break;
    }
}

void Dock::Refresh() {
    if (!Visible()) return;
    // A menu of ours briefly takes the focus; the app that had it still counts as active.
    HWND foreground = GetForegroundWindow();
    if (foreground == window_ || foreground == preview_.Window() || foreground == overlay_ || !foreground) {
        foreground = lastForeground_;
    } else {
        lastForeground_ = foreground;
    }
    flashing_.erase(foreground);
    for (auto it = flashing_.begin(); it != flashing_.end();) it = IsWindow(*it) ? std::next(it) : flashing_.erase(it);

    const bool all = allDesktops_;
    const auto& onSomeDesktop = callbacks_.onSomeDesktop;
    const auto include = [all, &onSomeDesktop](HWND window) {
        DWORD cloaked = 0;
        DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
        if (cloaked == 0) return true;  // on screen, on this desktop (or pinned to all of them)
        // Windows on other desktops are cloaked by the shell; so are a few hidden system windows, which have none.
        return all && (cloaked & DWM_CLOAKED_SHELL) && onSomeDesktop && onSomeDesktop(window);
    };
    const size_t before = model_.Apps().size();
    model_.Refresh(include, foreground, flashing_);
    if (model_.Apps().size() != before || layout_.width == 0) Relayout();
    Repaint();

    // Keep an open preview in step with the windows it shows.
    if (preview_.Shown() && previewFor_.part == DockPart::App) {
        const auto& apps = model_.Apps();
        if (previewFor_.index >= int(apps.size())) {
            HidePreview();
        } else {
            preview_.Update(apps[previewFor_.index], IconFor(apps[previewFor_.index]));
        }
    }
}

void Dock::Relayout() {
    if (!haveSlot_) return;
    const int maxWidth = int(slot_.right - slot_.left) - int(lroundf(48.0f * scale_));
    layout_ = LayoutDock(int(model_.Apps().size()), int(desktopNames_.size()), scale_, maxWidth, clockWidth_);
}

POINT Dock::Origin() const {
    const LONG slotWidth = slot_.right - slot_.left;
    const LONG slotHeight = slot_.bottom - slot_.top;
    LONG y = slot_.top + (slotHeight - layout_.height) / 2;
    y = std::min(y, slot_.bottom - LONG(layout_.height));
    return {slot_.left + (slotWidth - layout_.width) / 2, y};
}

const IconImage* Dock::IconFor(const DockApp& app) {
    const int size = layout_.iconSize > 0 ? layout_.iconSize : int(lroundf(30.0f * scale_));
    const std::wstring source = !app.iconSource.empty() ? app.iconSource : app.exe;
    if (source.empty() && !app.iconWindow) return nullptr;
    const std::wstring key = source + L"|" + std::to_wstring(size);
    auto found = icons_.find(key);
    if (found == icons_.end()) {
        IconImage image;
        // Our own exe carries no icon of its own; its windows show the Venom mark.
        static const std::wstring self = OwnExe();
        const bool ours = app.exe == self;
        bool ok = ours && app.iconWindow && WindowIcon(app.iconWindow, size, image);
        if (!ok) ok = ShellIcon(source, size, image);
        if (!ok && !app.exe.empty() && app.exe != source) ok = ShellIcon(app.exe, size, image);
        if (!ok && app.iconWindow) ok = WindowIcon(app.iconWindow, size, image);
        if (!ok) image = IconImage();  // remembered as "none", so it isn't looked up again every refresh
        found = icons_.emplace(key, std::move(image)).first;
    }
    return found->second.Empty() ? nullptr : &found->second;
}

void Dock::Repaint() {
    if (!Visible() || layout_.width <= 0 || !canvas_.Resize(layout_.width, layout_.height)) return;
    const auto& apps = model_.Apps();
    std::vector<const IconImage*> icons;
    for (const DockApp& app : apps) icons.push_back(IconFor(app));
    PaintDock(canvas_, layout_, apps, icons, state_);
    const POINT origin = Origin();
    canvas_.Present(window_, origin);
    const RECT placed = {origin.x, origin.y, origin.x + layout_.width, origin.y + layout_.height};
    if ((!placedShown_ || !EqualRect(&placed, &placedRect_)) && callbacks_.placed) {
        placedRect_ = placed;
        placedShown_ = true;
        callbacks_.placed(&placed);
    }
}

bool Dock::UpdateClockAndVolume() {
    wchar_t time[64] = {}, date[64] = {};
    GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, nullptr, nullptr, time, ARRAYSIZE(time));
    GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, nullptr, nullptr, date, ARRAYSIZE(date), nullptr);
    float level = 0.0f;
    bool muted = false;
    const bool hasAudio = volume_.Read(level, muted);
    const bool changed = state_.time != time || state_.date != date || state_.hasAudio != hasAudio ||
                         (hasAudio && (fabsf(state_.volume - level) > 0.004f || state_.muted != muted));
    state_.time = time;
    state_.date = date;
    state_.hasAudio = hasAudio;
    if (hasAudio) {
        state_.volume = level;
        state_.muted = muted;
    }
    return changed;
}

void Dock::Tick() {
    const ULONGLONG now = GetTickCount64();
    bool repaint = UpdateClockAndVolume();
    if (repaint && preview_.Shown() && previewFor_.part == DockPart::Volume) ShowPreview(previewFor_);  // new level
    bool attention = false;
    for (const DockApp& app : model_.Apps()) attention = attention || app.attention;
    if (attention) {
        state_.pulse = 0.5f + 0.5f * sinf(float(now - startTick_) * 0.0055f);
        repaint = true;
    }
    if (repaint) Repaint();

    // The preview follows the pointer: over an app (or button) for a moment shows it, away for a moment hides it.
    POINT cursor;
    GetCursorPos(&cursor);
    const HWND under = WindowFromPoint(cursor);
    if (under != window_ && state_.hover.part != DockPart::None) SetHover({});  // missed WM_MOUSELEAVE
    const bool overDock = under == window_ && state_.hover.part != DockPart::None;
    const bool overPreview = preview_.Shown() && under == preview_.Window();
    const bool buttonDown = (GetAsyncKeyState(VK_LBUTTON) | GetAsyncKeyState(VK_RBUTTON)) & 0x8000;
    if (overDock && !buttonDown && now >= blockedUntil_) {
        awaySince_ = 0;
        if (!preview_.Shown() || previewFor_ != state_.hover) {
            const bool app = state_.hover.part == DockPart::App;
            const ULONGLONG delay = preview_.Shown() ? (app ? 0 : 120) : (app ? kPreviewDelay : kLabelDelay);
            if (now - hoverSince_ >= delay) ShowPreview(state_.hover);
        }
    } else if (overPreview) {
        awaySince_ = 0;
    } else if (preview_.Shown()) {
        if (!awaySince_) awaySince_ = now;
        if (now - awaySince_ >= kHideDelay) HidePreview();
    }
}

void Dock::SetHover(DockHit hover) {
    if (hover == state_.hover) return;
    state_.hover = hover;
    hoverSince_ = GetTickCount64();
    Repaint();
}

void Dock::ActivateApp(int index) {
    const auto& apps = model_.Apps();
    if (index < 0 || index >= int(apps.size())) return;
    const DockApp& app = apps[index];
    if (app.windows.empty()) {
        LaunchAsUser(app.launch);
    } else {
        ToggleAppWindows(app.windows);
    }
}

void Dock::Click(DockHit hit) {
    switch (hit.part) {
        case DockPart::Start: PressKeys({VK_LWIN}); break;
        case DockPart::Search: PressKeys({VK_LWIN, 'S'}); break;
        case DockPart::App:
            ActivateApp(hit.index);
            ScheduleRefresh(150);
            break;
        case DockPart::Desktop:
            if (callbacks_.switchDesktop && hit.index != state_.currentDesktop) callbacks_.switchDesktop(hit.index);
            break;
        case DockPart::Volume:
            volume_.ToggleMute();
            if (UpdateClockAndVolume()) Repaint();
            break;
        case DockPart::Clock: PressKeys({VK_LWIN, 'N'}); break;  // notifications and the calendar
        default: break;
    }
}

void Dock::ShowMenu(int index) {
    const bool onApp = index >= 0 && index < int(model_.Apps().size());
    const DockApp app = onApp ? model_.Apps()[index] : DockApp();  // a copy: the model can refresh meanwhile
    HMENU menu = CreatePopupMenu();
    if (onApp) {
        AppendMenuW(menu, MF_STRING | MF_DISABLED, 0, app.name.c_str());
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        if (!app.launch.empty()) {
            AppendMenuW(menu, MF_STRING, kMenuOpen, app.windows.empty() ? L"Open" : L"New window");
            AppendMenuW(menu, MF_STRING, app.pinned ? kMenuUnpin : kMenuPin,
                        app.pinned ? L"Unpin from the dock" : L"Pin to the dock");
        }
        if (!app.windows.empty()) {
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, kMenuClose, app.windows.size() > 1 ? L"Close all windows" : L"Close window");
        }
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    }
    AppendMenuW(menu, MF_STRING, kMenuTaskManager, L"Task Manager");
    AppendMenuW(menu, MF_STRING, kMenuSettings, L"Dock settings…");

    POINT at;
    if (onApp) {
        const POINT origin = Origin();
        at = {origin.x + (layout_.apps[index].left + layout_.apps[index].right) / 2, origin.y + layout_.pill.top};
    } else {
        GetCursorPos(&at);
    }
    HidePreview();
    SetForegroundWindow(window_);  // so the menu closes when you click elsewhere
    const UINT command = UINT(TrackPopupMenu(
        menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_CENTERALIGN, at.x, at.y, 0, window_,
        nullptr));
    PostMessageW(window_, WM_NULL, 0, 0);
    DestroyMenu(menu);
    blockedUntil_ = GetTickCount64() + kAfterClick;

    switch (command) {
        case kMenuOpen: LaunchAsUser(app.launch); break;
        case kMenuPin:
        case kMenuUnpin:
            if (command == kMenuPin) {
                model_.Pin(app);
            } else {
                model_.Unpin(app);
            }
            if (callbacks_.pinsChanged) callbacks_.pinsChanged(model_.PinPaths());
            Refresh();
            break;
        case kMenuClose:
            for (const DockWindow& window : app.windows) PostMessageW(window.window, WM_CLOSE, 0, 0);
            ScheduleRefresh(400);
            break;
        case kMenuTaskManager: LaunchAsUser(L"taskmgr.exe"); break;
        case kMenuSettings:
            if (callbacks_.openSettings) callbacks_.openSettings();
            break;
    }
    // Give the focus back to the app that had it.
    if (lastForeground_ && IsWindow(lastForeground_) && GetForegroundWindow() == window_ && command != kMenuOpen)
        ActivateWindow(lastForeground_);
}

std::wstring Dock::LabelFor(DockHit hit) const {
    switch (hit.part) {
        case DockPart::Start: return L"Start  ·  right-click for more";
        case DockPart::Search: return L"Search";
        case DockPart::App: {
            const auto& apps = model_.Apps();
            return hit.index >= 0 && hit.index < int(apps.size()) ? apps[hit.index].name : L"";
        }
        case DockPart::Desktop:
            return hit.index >= 0 && hit.index < int(desktopNames_.size()) ? desktopNames_[hit.index] : L"";
        case DockPart::Volume: {
            if (!state_.hasAudio) return L"No speakers";
            if (state_.muted) return L"Muted  ·  scroll to change";
            return L"Volume " + std::to_wstring(int(lroundf(state_.volume * 100.0f))) + L"%  ·  scroll to change";
        }
        case DockPart::Clock: {
            wchar_t date[96] = {};
            GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_LONGDATE, nullptr, nullptr, date, ARRAYSIZE(date), nullptr);
            return date;
        }
        default: return L"";
    }
}

void Dock::ShowPreview(DockHit hit) {
    const auto& apps = model_.Apps();
    DockApp content;
    if (hit.part == DockPart::App && hit.index >= 0 && hit.index < int(apps.size())) {
        content = apps[hit.index];
    } else {
        content.name = LabelFor(hit);
    }
    if (content.name.empty() && content.windows.empty()) {
        HidePreview();
        return;
    }
    previewFor_ = hit;
    // Centred over what it describes, just above the dock, kept on the screen.
    const RECT part = DockPartRect(layout_, hit);
    const POINT origin = Origin();
    const RECT anchor = {origin.x + part.left, origin.y + layout_.pill.top, origin.x + part.right,
                         origin.y + layout_.pill.bottom};
    MONITORINFO monitor = {sizeof(monitor)};
    GetMonitorInfoW(MonitorFromRect(&slot_, MONITOR_DEFAULTTONEAREST), &monitor);
    preview_.Show(content, content.windows.empty() ? nullptr : IconFor(content), anchor, PreviewSide::Above,
                  monitor.rcMonitor, scale_);
    awaySince_ = 0;
}

void Dock::HidePreview() {
    preview_.Hide();
    previewFor_ = {};
    awaySince_ = 0;
}

LRESULT CALLBACK Dock::DockProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (g_dock && window == g_dock->window_) return g_dock->HandleDock(message, wParam, lParam);
    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT Dock::HandleDock(UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == shellHookMessage_ && message != 0) {
        OnShellEvent(wParam, reinterpret_cast<HWND>(lParam));
        return 0;
    }
    const POINT local = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    switch (message) {
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_MOUSEMOVE:
            SetHover(DockHitTest(layout_, local));
            if (!tracking_) {
                TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, window_, 0};
                tracking_ = TrackMouseEvent(&track) != FALSE;
            }
            return 0;
        case WM_MOUSELEAVE:
            tracking_ = false;
            SetHover({});
            return 0;
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
            pressed_ = DockHitTest(layout_, local);
            if (message != WM_RBUTTONDOWN) HidePreview();
            blockedUntil_ = GetTickCount64() + kAfterClick;
            return 0;
        case WM_LBUTTONUP: {
            const DockHit hit = DockHitTest(layout_, local);
            if (hit == pressed_) Click(hit);
            pressed_ = {};
            return 0;
        }
        case WM_MBUTTONUP: {
            // Middle click opens another one, as on the Windows taskbar.
            const DockHit hit = DockHitTest(layout_, local);
            if (hit == pressed_ && hit.part == DockPart::App) {
                LaunchAsUser(model_.Apps()[hit.index].launch);
                ScheduleRefresh(400);
            }
            pressed_ = {};
            return 0;
        }
        case WM_RBUTTONUP: {
            const DockHit hit = DockHitTest(layout_, local);
            pressed_ = {};
            switch (hit.part) {
                case DockPart::App: ShowMenu(hit.index); break;
                case DockPart::Start: PressKeys({VK_LWIN, 'X'}); break;  // the power-user menu
                case DockPart::Volume: OpenSettingsPage(L"ms-settings:sound"); break;
                case DockPart::Clock: OpenSettingsPage(L"ms-settings:dateandtime"); break;
                default: ShowMenu(-1); break;
            }
            return 0;
        }
        case WM_MOUSEWHEEL: {
            POINT point = local;  // wheel messages carry screen coordinates
            ScreenToClient(window_, &point);
            const DockHit hit = DockHitTest(layout_, point);
            const int notches = GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
            if (hit.part == DockPart::Volume && notches) {
                volume_.SetLevel(state_.volume + 0.02f * notches);
                if (UpdateClockAndVolume()) Repaint();
                if (preview_.Shown() && previewFor_.part == DockPart::Volume) ShowPreview(previewFor_);
            } else if (hit.part == DockPart::Desktop && notches && callbacks_.switchDesktop) {
                // Up for the previous desktop, down for the next.
                const int target = std::clamp(state_.currentDesktop - (notches > 0 ? 1 : -1), 0,
                                              int(desktopNames_.size()) - 1);
                if (target != state_.currentDesktop) callbacks_.switchDesktop(target);
            }
            return 0;
        }
        case WM_TIMER:
            if (wParam == kRefreshTimer) {
                KillTimer(window_, kRefreshTimer);
                Refresh();
            } else if (wParam == kTickTimer) {
                Tick();
            } else if (wParam == kPeriodicTimer) {
                Refresh();
            }
            return 0;
        case WM_SETTINGCHANGE:
        case WM_TIMECHANGE:
            // The clock format may have changed.
            clockWidth_ = DockClockWidth(scale_);
            Relayout();
            UpdateClockAndVolume();
            Repaint();
            return 0;
    }
    return DefWindowProcW(window_, message, wParam, lParam);
}
