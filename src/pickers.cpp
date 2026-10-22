#include "pickers.h"

#include <knownfolders.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>

#include "brand.h"
#include "zones.h"

namespace {

constexpr wchar_t kWindowClass[] = L"VenomMindAppPicker";
constexpr wchar_t kGlyphSearch = 0xE721;
AppPicker* g_picker = nullptr;

std::wstring Lower(std::wstring text) {
    if (!text.empty()) CharLowerBuffW(text.data(), DWORD(text.size()));
    return text;
}

bool EndsWith(const std::wstring& text, const wchar_t* suffix) {
    const size_t length = wcslen(suffix);
    return text.size() >= length && text.compare(text.size() - length, length, suffix) == 0;
}

// Start lists more than apps: uninstallers, readmes, help files and web links. None of those make good bubbles.
bool IsClutter(const std::wstring& name, const std::wstring& id) {
    const std::wstring n = Lower(name);
    const std::wstring i = Lower(id);
    if (n.find(L"uninstall") != std::wstring::npos || i.find(L"uninstall") != std::wstring::npos) return true;
    if (i.rfind(L"http:", 0) == 0 || i.rfind(L"https:", 0) == 0) return true;
    for (const wchar_t* document : {L".url", L".chm", L".pdf", L".txt", L".htm", L".html", L".rtf", L".hlp"})
        if (EndsWith(i, document)) return true;
    return false;
}

// A picked item's name and how to open it again.
bool Describe(IShellItem* item, std::wstring& name, std::wstring& target) {
    PWSTR text = nullptr;
    if (SUCCEEDED(item->GetDisplayName(SIGDN_NORMALDISPLAY, &text)) && text) {
        name = text;
        CoTaskMemFree(text);
    }
    text = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &text)) || !text) {
        text = nullptr;
        if (FAILED(item->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING, &text)) || !text) return false;
    }
    target = text;
    CoTaskMemFree(text);
    if (name.empty()) name = target;
    return !target.empty();
}

float Mid(LONG a, LONG b) { return (a + b) * 0.5f; }

std::wstring Ellipsize(const std::wstring& text, float maxWidth, int size, int weight, const wchar_t* family) {
    if (Canvas::MeasureString(text, size, weight, family) <= maxWidth) return text;
    size_t low = 0, high = text.size();
    while (low < high) {
        const size_t mid = (low + high + 1) / 2;
        if (Canvas::MeasureString(text.substr(0, mid) + L"…", size, weight, family) <= maxWidth) {
            low = mid;
        } else {
            high = mid - 1;
        }
    }
    return text.substr(0, low) + L"…";
}

}  // namespace

std::vector<InstalledApp> ListInstalledApps() {
    std::vector<InstalledApp> apps;
    ComPtr<IShellItem> folder;
    if (FAILED(SHGetKnownFolderItem(FOLDERID_AppsFolder, KF_FLAG_DEFAULT, nullptr, IID_PPV_ARGS(&folder)))) return apps;
    ComPtr<IEnumShellItems> items;
    if (FAILED(folder->BindToHandler(nullptr, BHID_EnumItems, IID_PPV_ARGS(&items)))) return apps;
    ComPtr<IShellItem> item;
    while (items->Next(1, item.ReleaseAndGetAddressOf(), nullptr) == S_OK) {
        PWSTR name = nullptr;
        PWSTR id = nullptr;
        if (SUCCEEDED(item->GetDisplayName(SIGDN_NORMALDISPLAY, &name)) && name &&
            SUCCEEDED(item->GetDisplayName(SIGDN_PARENTRELATIVEPARSING, &id)) && id && !IsClutter(name, id)) {
            apps.push_back({name, std::wstring(L"shell:AppsFolder\\") + id});
        }
        CoTaskMemFree(name);
        CoTaskMemFree(id);
    }
    std::sort(apps.begin(), apps.end(), [](const InstalledApp& a, const InstalledApp& b) {
        const int order = _wcsicmp(a.name.c_str(), b.name.c_str());
        return order != 0 ? order < 0 : a.target < b.target;
    });
    apps.erase(std::unique(apps.begin(), apps.end(),
                           [](const InstalledApp& a, const InstalledApp& b) { return a.target == b.target; }),
               apps.end());
    return apps;
}

bool PickFolder(HWND owner, std::wstring& name, std::wstring& target) {
    ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) return false;
    FILEOPENDIALOGOPTIONS options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS);
    dialog->SetTitle(L"Add a folder to the margin");
    dialog->SetOkButtonLabel(L"Add folder");
    if (dialog->Show(owner) != S_OK) return false;
    ComPtr<IShellItem> item;
    return SUCCEEDED(dialog->GetResult(&item)) && Describe(item.Get(), name, target);
}

bool PickProgram(HWND owner, std::wstring& name, std::wstring& target) {
    ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) return false;
    const COMDLG_FILTERSPEC types[] = {{L"Programs and shortcuts", L"*.exe;*.lnk;*.bat;*.cmd;*.ps1;*.appref-ms"},
                                       {L"All files", L"*.*"}};
    dialog->SetFileTypes(ARRAYSIZE(types), types);
    FILEOPENDIALOGOPTIONS options = 0;
    dialog->GetOptions(&options);
    // Keep a chosen shortcut as itself, so its arguments and icon come along.
    dialog->SetOptions(options | FOS_FILEMUSTEXIST | FOS_NODEREFERENCELINKS);
    dialog->SetTitle(L"Add an app to the margin");
    dialog->SetOkButtonLabel(L"Add app");
    ComPtr<IShellItem> programs;
    if (SUCCEEDED(SHGetKnownFolderItem(FOLDERID_ProgramFiles, KF_FLAG_DEFAULT, nullptr, IID_PPV_ARGS(&programs))))
        dialog->SetDefaultFolder(programs.Get());
    if (dialog->Show(owner) != S_OK) return false;
    ComPtr<IShellItem> item;
    return SUCCEEDED(dialog->GetResult(&item)) && Describe(item.Get(), name, target);
}

// ---- The view ----

void AppPickerView::Build(float scale) {
    scale_ = scale;
    const auto px = [scale](float v) { return LONG(lroundf(v * scale)); };
    width_ = px(440);
    const LONG pad = px(20);
    close_ = {width_ - px(46), px(12), width_ - px(14), px(44)};
    search_ = {pad, px(62), width_ - pad, px(62 + 40)};
    rowHeight_ = px(40);
    // Ten whole rows, with the footer right under them.
    const LONG listTop = px(114);
    list_ = {px(10), listTop, width_ - px(10), listTop + 10 * rowHeight_};
    height_ = list_.bottom + px(54);
    browse_ = {pad, height_ - px(44), pad + px(190), height_ - px(14)};
}

void AppPickerView::SetApps(const std::vector<InstalledApp>* apps) {
    apps_ = apps;
    SetFilter(filter_);
}

void AppPickerView::SetFilter(const std::wstring& filter) {
    filter_ = filter;
    matches_.clear();
    const std::wstring wanted = Lower(filter);
    if (apps_) {
        for (size_t i = 0; i < apps_->size(); ++i)
            if (wanted.empty() || Lower((*apps_)[i].name).find(wanted) != std::wstring::npos) matches_.push_back(int(i));
        // Names that start with what was typed come first.
        if (!wanted.empty())
            std::stable_sort(matches_.begin(), matches_.end(), [&](int a, int b) {
                return Lower((*apps_)[a].name).rfind(wanted, 0) == 0 && Lower((*apps_)[b].name).rfind(wanted, 0) != 0;
            });
    }
    scroll_ = 0;
    selected_ = matches_.empty() ? -1 : 0;
}

const InstalledApp* AppPickerView::Match(int match) const {
    return apps_ && match >= 0 && match < int(matches_.size()) ? &(*apps_)[matches_[match]] : nullptr;
}

int AppPickerView::VisibleRows() const { return rowHeight_ ? (list_.bottom - list_.top) / rowHeight_ : 0; }

void AppPickerView::Scroll(int rows) {
    scroll_ = std::clamp(scroll_ + rows, 0, std::max(0, Matches() - VisibleRows()));
}

void AppPickerView::MoveSelection(int delta) {
    if (matches_.empty()) return;
    selected_ = std::clamp(selected_ + delta, 0, Matches() - 1);
    if (selected_ < scroll_) scroll_ = selected_;
    if (selected_ >= scroll_ + VisibleRows()) scroll_ = selected_ - VisibleRows() + 1;
}

RECT AppPickerView::RowRect(int visibleIndex) const {
    return {list_.left, list_.top + visibleIndex * rowHeight_, list_.right - LONG(lroundf(8.0f * scale_)),
            list_.top + (visibleIndex + 1) * rowHeight_};
}

AppPickerView::Part AppPickerView::HitTest(POINT p, int* match) const {
    *match = -1;
    if (PtInRect(&close_, p)) return Part::Close;
    if (PtInRect(&search_, p)) return Part::Search;
    if (PtInRect(&browse_, p)) return Part::Browse;
    if (PtInRect(&list_, p)) {
        const int row = scroll_ + (p.y - list_.top) / std::max(1, rowHeight_);
        if (row < Matches()) {
            *match = row;
            return Part::Row;
        }
        return Part::None;
    }
    if (p.y < search_.top) return Part::Title;
    return Part::None;
}

void AppPickerView::Paint(Canvas& canvas, Part hover, int hoverMatch,
                          const std::function<const IconImage*(const InstalledApp&)>& icon) const {
    const float s = scale_;
    const auto px = [s](float v) { return int(lroundf(v * s)); };
    const float width = float(width_), height = float(height_);
    const float radius = 14.0f * s;
    const wchar_t* ui = venom::UiFont();
    canvas.Clear();
    // Black glass with the Venom rim, like the settings page.
    canvas.FillRoundRect(0, 0, width, height, radius, venom::WithAlpha(venom::kVoid, 0.985f));
    canvas.StrokeRoundRect(0, 0, width, height, radius, 1.2f * s, venom::WithAlpha(venom::kFang, 0.55f));
    canvas.FillRoundRect(width * 0.5f, 0, width - radius, 1.2f * s, 0.0f, venom::WithAlpha(venom::kSheen, 0.55f));

    const float titleY = 28.0f * s;
    venom::DrawMark(canvas, 20.0f * s + 11.0f * s, titleY, 24.0f * s, venom::MarkStyle::Glow, true);
    canvas.DrawString(L"Add an app", 20.0f * s + 30.0f * s, titleY, px(16), FW_BOLD, venom::kBone, venom::DisplayFont());
    if (hover == Part::Close)
        canvas.FillRoundRect(float(close_.left), float(close_.top), float(close_.right), float(close_.bottom), 8.0f * s,
                             venom::WithAlpha(venom::kFang, 0.25f));
    const float xc = Mid(close_.left, close_.right), yc = Mid(close_.top, close_.bottom), arm = 5.5f * s;
    canvas.DrawLine(xc - arm, yc - arm, xc + arm, yc + arm, 1.6f * s, venom::kBone);
    canvas.DrawLine(xc - arm, yc + arm, xc + arm, yc - arm, 1.6f * s, venom::kBone);

    // The search field, always typing.
    const RECT& f = search_;
    canvas.FillRoundRect(float(f.left), float(f.top), float(f.right), float(f.bottom), 10.0f * s, venom::kCard);
    canvas.StrokeRoundRect(float(f.left), float(f.top), float(f.right), float(f.bottom), 10.0f * s, 1.2f * s,
                           venom::WithAlpha(venom::kFang, 0.7f));
    const float fy = Mid(f.top, f.bottom);
    const float glyphWidth = Canvas::MeasureString(std::wstring(1, kGlyphSearch), px(14), FW_NORMAL, venom::IconFont());
    canvas.DrawString(std::wstring(1, kGlyphSearch), f.left + 14.0f * s, fy, px(14), FW_NORMAL, venom::kBoneDim,
                      venom::IconFont());
    const float textX = f.left + 14.0f * s + glyphWidth + 10.0f * s;
    float caretX = textX;
    if (filter_.empty()) {
        canvas.DrawString(L"Type to search your apps", textX, fy, px(14), FW_NORMAL, venom::kBoneFaint, ui);
    } else {
        caretX += canvas.DrawString(filter_, textX, fy, px(14), FW_SEMIBOLD, venom::kBone, ui) + 1.0f * s;
    }
    canvas.FillRoundRect(caretX, fy - 9.0f * s, caretX + 1.5f * s, fy + 9.0f * s, 0.0f, venom::kFang);

    // The list.
    const int rows = VisibleRows();
    for (int v = 0; v < rows; ++v) {
        const int match = scroll_ + v;
        const InstalledApp* app = Match(match);
        if (!app) break;
        const RECT r = RowRect(v);
        const bool selected = match == selected_;
        const bool hot = hover == Part::Row && hoverMatch == match;
        if (selected || hot)
            canvas.FillRoundRect(float(r.left), float(r.top + px(2)), float(r.right), float(r.bottom - px(2)), 8.0f * s,
                                 selected ? venom::WithAlpha(venom::kFang, 0.16f) : venom::WithAlpha(venom::kCardHi, 0.9f));
        if (selected)
            canvas.FillRoundRect(float(r.left), Mid(r.top, r.bottom) - 9.0f * s, float(r.left) + 3.0f * s,
                                 Mid(r.top, r.bottom) + 9.0f * s, 1.5f * s, venom::kFang);
        const int iconSize = px(24);
        const IconImage* image = icon ? icon(*app) : nullptr;
        const float iconX = r.left + 14.0f * s, cy = Mid(r.top, r.bottom);
        if (image && !image->Empty()) {
            canvas.DrawPixels(image->pixels.data(), image->size, image->size, int(iconX),
                              int(lroundf(cy - image->size * 0.5f)));
        } else {
            canvas.FillRoundRect(iconX, cy - iconSize * 0.5f, iconX + iconSize, cy + iconSize * 0.5f, 5.0f * s,
                                 venom::kCardHi);
        }
        const float nameX = iconX + iconSize + 12.0f * s;
        const std::wstring name = Ellipsize(app->name, r.right - nameX - 10.0f * s, px(13), FW_SEMIBOLD, ui);
        canvas.DrawString(name, nameX, cy, px(13), FW_SEMIBOLD, selected || hot ? venom::kBone : venom::kBoneDim, ui);
    }
    if (Matches() == 0)
        canvas.DrawString(apps_ && !apps_->empty() ? L"No app matches that" : L"Reading your apps…",
                          float(list_.left + px(14)), float(list_.top + px(20)), px(13), FW_NORMAL, venom::kBoneFaint, ui);
    // A thin scroll bar when there's more than fits.
    if (Matches() > rows && rows > 0) {
        const float trackTop = float(list_.top), trackBottom = float(list_.bottom);
        const float thumb = std::max(24.0f * s, (trackBottom - trackTop) * rows / Matches());
        const float top = trackTop + (trackBottom - trackTop - thumb) * scroll_ / std::max(1, Matches() - rows);
        canvas.FillRoundRect(float(list_.right) - 4.0f * s, top, float(list_.right) - 1.0f * s, top + thumb, 1.5f * s,
                             venom::WithAlpha(venom::kBoneFaint, 0.8f));
    }

    // Footer.
    canvas.FillRoundRect(float(px(20)), float(list_.bottom) + 4.0f * s, width - px(20), float(list_.bottom) + 5.0f * s,
                         0.0f, venom::kCardHi);
    const Rgba link = {0.55f, 0.62f, 1.0f, hover == Part::Browse ? 1.0f : 0.8f};
    const float linkWidth = canvas.DrawString(L"Browse for a program…", float(browse_.left), Mid(browse_.top, browse_.bottom),
                                              px(13), FW_SEMIBOLD, link, ui);
    if (hover == Part::Browse)
        canvas.FillRoundRect(float(browse_.left), Mid(browse_.top, browse_.bottom) + 9.0f * s, browse_.left + linkWidth,
                             Mid(browse_.top, browse_.bottom) + 10.0f * s, 0.0f, link);
    const std::wstring count = std::to_wstring(Matches()) + (Matches() == 1 ? L" app" : L" apps");
    const float countWidth = Canvas::MeasureString(count, px(12), FW_NORMAL, ui);
    canvas.DrawString(count, width - px(20) - countWidth, Mid(browse_.top, browse_.bottom), px(12), FW_NORMAL,
                      venom::kBoneFaint, ui);
}

// ---- The window ----

bool AppPicker::Init(HINSTANCE instance, Chosen chosen) {
    g_picker = this;
    instance_ = instance;
    chosen_ = std::move(chosen);
    WNDCLASSEXW windowClass = {sizeof(windowClass)};
    windowClass.lpfnWndProc = Proc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = kWindowClass;
    return RegisterClassExW(&windowClass) != 0;
}

void AppPicker::Show(HMONITOR monitor) {
    if (window_) {
        SetForegroundWindow(window_);
        return;
    }
    scale_ = MonitorScale(monitor);
    view_.Build(scale_);
    apps_ = ListInstalledApps();  // read fresh each time, so newly installed apps are there
    view_.SetApps(&apps_);
    view_.SetFilter(L"");
    const RECT work = WorkArea(monitor);
    const int x = (work.left + work.right - view_.Width()) / 2;
    const int y = (work.top + work.bottom - view_.Height()) / 2;
    window_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW, kWindowClass, L"Add an app", WS_POPUP, x,
                              y, view_.Width(), view_.Height(), nullptr, nullptr, instance_, nullptr);
    if (!window_) return;
    hover_ = AppPickerView::Part::None;
    hoverMatch_ = -1;
    Repaint();
    ShowWindow(window_, SW_SHOW);
    SetForegroundWindow(window_);
    SetFocus(window_);
}

void AppPicker::Close() {
    if (!window_) return;
    const HWND window = window_;
    window_ = nullptr;
    DestroyWindow(window);
}

const IconImage* AppPicker::IconFor(const InstalledApp& app) {
    auto found = icons_.find(app.target);
    if (found == icons_.end()) {
        IconImage icon;
        if (!ShellIcon(app.target, int(lroundf(24.0f * scale_)), icon)) icon = IconImage();
        found = icons_.emplace(app.target, std::move(icon)).first;
    }
    return found->second.Empty() ? nullptr : &found->second;
}

void AppPicker::Repaint() {
    if (!window_ || !canvas_.Resize(view_.Width(), view_.Height())) return;
    view_.Paint(canvas_, hover_, hoverMatch_, [this](const InstalledApp& app) { return IconFor(app); });
    RECT rect;
    GetWindowRect(window_, &rect);
    canvas_.Present(window_, {rect.left, rect.top});
}

void AppPicker::Choose(int match) {
    const InstalledApp* app = view_.Match(match);
    if (!app) return;
    const InstalledApp chosen = *app;
    Close();
    if (chosen_) chosen_(chosen);
}

void AppPicker::Browse() {
    browsing_ = true;
    InstalledApp app;
    const bool picked = PickProgram(window_, app.name, app.target);
    browsing_ = false;
    if (!picked) {
        if (window_) SetForegroundWindow(window_);
        return;
    }
    Close();
    if (chosen_) chosen_(app);
}

LRESULT CALLBACK AppPicker::Proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (g_picker && window == g_picker->window_) return g_picker->Handle(message, wParam, lParam);
    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT AppPicker::Handle(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_NCHITTEST: {
            // The title drags the window; everything else is ours.
            POINT p = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ScreenToClient(window_, &p);
            int match = -1;
            return view_.HitTest(p, &match) == AppPickerView::Part::Title ? HTCAPTION : HTCLIENT;
        }
        case WM_MOUSEMOVE: {
            int match = -1;
            const AppPickerView::Part part = view_.HitTest({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}, &match);
            if (part != hover_ || match != hoverMatch_) {
                hover_ = part;
                hoverMatch_ = match;
                Repaint();
            }
            if (!tracking_) {
                TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, window_, 0};
                tracking_ = TrackMouseEvent(&track) != FALSE;
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            tracking_ = false;
            hover_ = AppPickerView::Part::None;
            hoverMatch_ = -1;
            Repaint();
            return 0;
        case WM_SETCURSOR:
            if (LOWORD(lParam) == HTCLIENT) {
                const bool hand = hover_ == AppPickerView::Part::Row || hover_ == AppPickerView::Part::Close ||
                                  hover_ == AppPickerView::Part::Browse;
                SetCursor(LoadCursorW(nullptr, hover_ == AppPickerView::Part::Search ? IDC_IBEAM : hand ? IDC_HAND : IDC_ARROW));
                return TRUE;
            }
            break;
        case WM_LBUTTONUP: {
            int match = -1;
            switch (view_.HitTest({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}, &match)) {
                case AppPickerView::Part::Close: Close(); break;
                case AppPickerView::Part::Row: Choose(match); break;
                case AppPickerView::Part::Browse: Browse(); break;
                default: break;
            }
            return 0;
        }
        case WM_MOUSEWHEEL:
            view_.Scroll(-GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA * 3);
            Repaint();
            return 0;
        case WM_CHAR:
            if (wParam == VK_BACK) {
                std::wstring filter = view_.Filter();
                if (!filter.empty()) filter.pop_back();
                view_.SetFilter(filter);
                Repaint();
            } else if (wParam >= 32 && wParam != 127) {
                view_.SetFilter(view_.Filter() + wchar_t(wParam));
                Repaint();
            }
            return 0;
        case WM_KEYDOWN:
            switch (wParam) {
                case VK_ESCAPE:
                    // First clears the search, then closes.
                    if (!view_.Filter().empty()) {
                        view_.SetFilter(L"");
                        Repaint();
                    } else {
                        Close();
                    }
                    return 0;
                case VK_RETURN: Choose(view_.Selected()); return 0;
                case VK_UP: view_.MoveSelection(-1); break;
                case VK_DOWN: view_.MoveSelection(1); break;
                case VK_PRIOR: view_.MoveSelection(-view_.VisibleRows()); break;
                case VK_NEXT: view_.MoveSelection(view_.VisibleRows()); break;
                case VK_HOME: view_.MoveSelection(-view_.Matches()); break;
                case VK_END: view_.MoveSelection(view_.Matches()); break;
                default: return 0;
            }
            Repaint();
            return 0;
        case WM_ACTIVATE:
            // Like a menu: clicking anywhere else puts it away (but not while Windows' file picker is open over it).
            if (LOWORD(wParam) == WA_INACTIVE && !browsing_) PostMessageW(window_, WM_CLOSE, 0, 0);
            return 0;
        case WM_CLOSE:
            Close();
            return 0;
    }
    return DefWindowProcW(window_, message, wParam, lParam);
}
