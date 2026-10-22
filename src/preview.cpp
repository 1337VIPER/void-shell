#include "dock.h"

#include <windowsx.h>

#include <algorithm>
#include <cmath>

namespace {

constexpr wchar_t kPreviewClass[] = L"VenomMindPreview";

}  // namespace

void ActivateWindow(HWND window) {
    if (IsIconic(window)) ShowWindowAsync(window, SW_RESTORE);
    // The click on the dock or a bubble was the last input, so Windows lets us hand the focus on.
    if (!SetForegroundWindow(window) || GetForegroundWindow() != window) SwitchToThisWindow(window, TRUE);
}

void ToggleAppWindows(const std::vector<DockWindow>& windows) {
    if (windows.empty()) return;
    const HWND foreground = GetForegroundWindow();
    if (windows.size() == 1) {
        // One window: bring it up, or put it away if it's already in front.
        const HWND window = windows[0].window;
        if (window == foreground && !IsIconic(window)) {
            ShowWindowAsync(window, SW_MINIMIZE);
        } else {
            ActivateWindow(window);
        }
        return;
    }
    // Several: the most recent one first, then each click moves on to the next.
    size_t pick = 0;
    bool inFront = false;
    for (size_t i = 0; i < windows.size(); ++i) {
        if (windows[i].window == foreground) {
            pick = (i + 1) % windows.size();
            inFront = true;
        }
    }
    if (!inFront) {
        for (size_t i = 0; i < windows.size(); ++i)
            if (windows[i].z < windows[pick].z) pick = i;
    }
    ActivateWindow(windows[pick].window);
}

RECT PlacePreview(SIZE size, const RECT& anchor, PreviewSide side, const RECT& bounds, int gap) {
    LONG x = 0, y = 0;
    const LONG midX = (anchor.left + anchor.right) / 2, midY = (anchor.top + anchor.bottom) / 2;
    switch (side) {
        case PreviewSide::Above: x = midX - size.cx / 2; y = anchor.top - gap - size.cy; break;
        case PreviewSide::Below: x = midX - size.cx / 2; y = anchor.bottom + gap; break;
        case PreviewSide::Left: x = anchor.left - gap - size.cx; y = midY - size.cy / 2; break;
        case PreviewSide::Right: x = anchor.right + gap; y = midY - size.cy / 2; break;
    }
    // Kept on the screen.
    x = std::clamp(x, bounds.left + gap, std::max(bounds.left + gap, bounds.right - size.cx - gap));
    y = std::clamp(y, bounds.top + gap, std::max(bounds.top + gap, bounds.bottom - size.cy - gap));
    return {x, y, x + size.cx, y + size.cy};
}

bool WindowPreview::Init(HINSTANCE instance, Callbacks callbacks) {
    callbacks_ = std::move(callbacks);
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW windowClass = {sizeof(windowClass)};
        windowClass.lpfnWndProc = Proc;
        windowClass.hInstance = instance;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.lpszClassName = kPreviewClass;
        registered = RegisterClassExW(&windowClass) != 0;
    }
    // Not layered, so DWM can draw live window thumbnails into it.
    window_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, kPreviewClass, L"VenomMind preview",
                              WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, instance, this);
    if (!window_) return false;
    const DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_ROUND;
    DwmSetWindowAttribute(window_, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));
    const COLORREF border = RGB(255, 42, 79);  // Venom crimson
    DwmSetWindowAttribute(window_, DWMWA_BORDER_COLOR, &border, sizeof(border));
    return true;
}

void WindowPreview::Shutdown() {
    Hide();
    if (window_) DestroyWindow(window_);
    window_ = nullptr;
}

void WindowPreview::ReleaseThumbnails() {
    for (const HTHUMBNAIL thumbnail : thumbnails_)
        if (thumbnail) DwmUnregisterThumbnail(thumbnail);
    thumbnails_.clear();
    live_.clear();
}

void WindowPreview::Show(const DockApp& content, const IconImage* icon, const RECT& anchor, PreviewSide side,
                         const RECT& bounds, float scale) {
    if (!window_) return;
    ReleaseThumbnails();
    content_ = content;
    icon_ = icon ? *icon : IconImage();
    anchor_ = anchor;
    side_ = side;
    bounds_ = bounds;
    scale_ = scale;
    layout_ = LayoutPreview(content_, scale_);
    for (size_t i = 0; i < layout_.cells.size(); ++i) {
        HTHUMBNAIL thumbnail = nullptr;
        SIZE source = {};
        bool live = SUCCEEDED(DwmRegisterThumbnail(window_, content_.windows[i].window, &thumbnail)) &&
                    SUCCEEDED(DwmQueryThumbnailSourceSize(thumbnail, &source));
        // A minimized window can come back as just its caption; show the icon rather than a sliver.
        live = live && source.cx >= 64 && source.cy >= 48;
        if (!live && thumbnail) {
            DwmUnregisterThumbnail(thumbnail);
            thumbnail = nullptr;
        }
        thumbnails_.push_back(thumbnail);
        live_.push_back(live);
    }
    const RECT place = PlacePreview({layout_.width, layout_.height}, anchor_, side_, bounds_, int(lroundf(6.0f * scale_)));
    hover_ = -1;
    hoverClose_ = false;
    Paint();
    SetWindowPos(window_, HWND_TOPMOST, place.left, place.top, layout_.width, layout_.height,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    PlaceThumbnails();
    shown_ = true;
}

void WindowPreview::Update(const DockApp& content, const IconImage* icon) {
    if (!shown_) return;
    bool same = content.windows.size() == content_.windows.size() && content.name == content_.name;
    for (size_t i = 0; same && i < content.windows.size(); ++i) same = content.windows[i].window == content_.windows[i].window;
    if (!same) {
        // Windows came or went: lay it out again, or put it away once there's nothing left to show.
        if (content.windows.empty() && !content_.windows.empty()) {
            Hide();
        } else {
            Show(content, icon, anchor_, side_, bounds_, scale_);
        }
        return;
    }
    content_ = content;  // titles may have changed
    Paint();
}

void WindowPreview::Hide() {
    ReleaseThumbnails();
    if (window_) ShowWindow(window_, SW_HIDE);
    shown_ = false;
}

void WindowPreview::PlaceThumbnails() {
    for (size_t i = 0; i < thumbnails_.size() && i < layout_.thumbs.size(); ++i) {
        if (!thumbnails_[i]) continue;
        SIZE source = {};
        DwmQueryThumbnailSourceSize(thumbnails_[i], &source);
        const RECT& box = layout_.thumbs[i];
        const float boxWidth = float(box.right - box.left), boxHeight = float(box.bottom - box.top);
        const float fit = std::min({boxWidth / std::max(1L, source.cx), boxHeight / std::max(1L, source.cy), 1.0f});
        const LONG width = LONG(source.cx * fit), height = LONG(source.cy * fit);
        DWM_THUMBNAIL_PROPERTIES properties = {};
        properties.dwFlags = DWM_TNP_RECTDESTINATION | DWM_TNP_VISIBLE | DWM_TNP_OPACITY | DWM_TNP_SOURCECLIENTAREAONLY;
        properties.rcDestination = {box.left + LONG(boxWidth - width) / 2, box.top + LONG(boxHeight - height) / 2,
                                    box.left + LONG(boxWidth - width) / 2 + width,
                                    box.top + LONG(boxHeight - height) / 2 + height};
        properties.fVisible = TRUE;
        properties.opacity = 255;
        properties.fSourceClientAreaOnly = FALSE;
        DwmUpdateThumbnailProperties(thumbnails_[i], &properties);
    }
}

void WindowPreview::Paint() {
    if (!canvas_.Resize(layout_.width, layout_.height)) return;
    PaintPreview(canvas_, layout_, content_, icon_.Empty() ? nullptr : &icon_, live_, hover_, hoverClose_, scale_);
    InvalidateRect(window_, nullptr, FALSE);
}

LRESULT CALLBACK WindowPreview::Proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* self = reinterpret_cast<WindowPreview*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (self && self->window_ == window) return self->Handle(message, wParam, lParam);
    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT WindowPreview::Handle(UINT message, WPARAM wParam, LPARAM lParam) {
    const POINT local = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    switch (message) {
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT paint;
            HDC dc = BeginPaint(window_, &paint);
            canvas_.Blit(dc, 0, 0);
            EndPaint(window_, &paint);
            return 0;
        }
        case WM_MOUSEMOVE: {
            bool onClose = false;
            const int cell = PreviewCellAt(layout_, local, &onClose);
            if (cell != hover_ || onClose != hoverClose_) {
                hover_ = cell;
                hoverClose_ = onClose;
                Paint();
            }
            if (!tracking_) {
                TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, window_, 0};
                tracking_ = TrackMouseEvent(&track) != FALSE;
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            tracking_ = false;
            if (hover_ != -1) {
                hover_ = -1;
                hoverClose_ = false;
                Paint();
            }
            return 0;
        case WM_LBUTTONUP:
        case WM_MBUTTONUP: {
            bool onClose = false;
            const int cell = PreviewCellAt(layout_, local, &onClose);
            if (cell < 0 || cell >= int(content_.windows.size())) return 0;
            const HWND window = content_.windows[cell].window;
            if (onClose || message == WM_MBUTTONUP) {
                PostMessageW(window, WM_CLOSE, 0, 0);  // the owner refreshes the preview once it's gone
                if (callbacks_.closed) callbacks_.closed();
            } else {
                Hide();
                ActivateWindow(window);
                if (callbacks_.picked) callbacks_.picked();
            }
            return 0;
        }
    }
    return DefWindowProcW(window_, message, wParam, lParam);
}
