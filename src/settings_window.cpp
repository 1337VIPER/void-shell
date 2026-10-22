#include "settings_window.h"

#include <windowsx.h>

#include <algorithm>
#include <cmath>

#include "brand.h"
#include "zones.h"

namespace {

constexpr wchar_t kWindowClass[] = L"VoidShellSettings";
constexpr const wchar_t* kTabNames[kSettingsTabs] = {L"The void", L"Frame", L"Dock", L"Zones", L"Startup"};

// The Venom palette, as the settings page uses it.
constexpr Rgba kText = venom::kBone;
constexpr Rgba kDim = venom::WithAlpha(venom::kBoneDim, 0.9f);
constexpr Rgba kSection = venom::WithAlpha(venom::kFang, 0.9f);
constexpr Rgba kField = venom::kCard;
constexpr Rgba kFieldEdge = venom::WithAlpha(venom::kCardHi, 1.0f);
constexpr Rgba kHover = venom::WithAlpha(venom::kFang, 0.06f);
constexpr Rgba kLink = {0.55f, 0.62f, 1.0f, 1.0f};  // sheen, lifted to read on black
constexpr Rgba kWarn = {1.0f, 0.82f, 0.24f, 0.95f};  // --warn #ffd23d

Rgba Faded(Rgba color, float amount) { return {color.r, color.g, color.b, color.a * amount}; }

float Mid(const RECT& r) { return (r.top + r.bottom) * 0.5f; }

}  // namespace

void SettingsView::Build(const Settings& settings, const SettingsContext& context, float scale, int tab) {
    scale_ = scale;
    brand_ = settings.brandName.empty() ? L"VenomMind" : settings.brandName;
    // The window keeps one height for every tab, so switching tabs doesn't make it jump.
    LONG tallest = 0;
    for (int t = 0; t < kSettingsTabs; ++t) tallest = std::max(tallest, BuildTab(settings, context, t));
    tab_ = std::clamp(tab, 0, kSettingsTabs - 1);
    const LONG bottom = BuildTab(settings, context, tab_);

    // Footer links, below the tallest tab.
    const auto px = [scale](float v) { return LONG(lroundf(v * scale)); };
    const LONG pad = px(28);
    const LONG footer = tallest + px(22);
    Item log = {};
    log.kind = Kind::Link;
    log.id = SettingId::OpenLog;
    log.label = L"Open log";
    log.enabled = true;
    log.rect = {pad, footer, pad + px(90), footer + px(36)};
    log.control = {pad, footer + px(6), pad + px(80), footer + px(30)};
    Item folder = log;
    folder.id = SettingId::OpenFolder;
    folder.label = L"Open folder";
    folder.rect = {pad + px(100), footer, pad + px(210), footer + px(36)};
    folder.control = {pad + px(100), footer + px(6), pad + px(200), footer + px(30)};
    items_.push_back(log);
    items_.push_back(folder);
    height_ = footer + px(44);
    (void)bottom;
}

LONG SettingsView::BuildTab(const Settings& settings, const SettingsContext& context, int tab) {
    const float scale = scale_;
    const auto px = [scale](float v) { return LONG(lroundf(v * scale)); };
    width_ = px(620);
    titleHeight_ = px(60);
    tabsBottom_ = titleHeight_ + px(46);
    const LONG pad = px(28);
    const LONG right = width_ - pad;
    close_ = {width_ - px(50), px(14), width_ - px(18), px(46)};
    items_.clear();

    // The tab strip, under the title.
    tabs_.clear();
    LONG tabX = pad;
    for (int t = 0; t < kSettingsTabs; ++t) {
        const LONG w = LONG(Canvas::MeasureString(kTabNames[t], px(13), FW_SEMIBOLD, venom::UiFont())) + px(24);
        tabs_.push_back({tabX - px(12), titleHeight_ + px(6), tabX - px(12) + w, tabsBottom_ - px(4)});
        tabX += w + px(4);
    }

    LONG y = tabsBottom_ + px(6);
    const auto add = [&](Kind kind, SettingId id, LONG height, std::wstring label, std::wstring detail = L"") {
        Item item = {};
        item.kind = kind;
        item.id = id;
        item.label = std::move(label);
        item.detail = std::move(detail);
        item.enabled = true;
        item.rect = {pad - px(10), y, right + px(10), y + height};
        y += height;
        items_.push_back(std::move(item));
        return &items_.back();
    };
    const auto section = [&](const wchar_t* title) { add(Kind::Section, SettingId::None, px(46), title); };
    const auto toggle = [&](SettingId id, const wchar_t* label, std::wstring detail, bool on, bool enabled = true) {
        Item* item = add(Kind::Toggle, id, detail.empty() ? px(44) : px(58), label, std::move(detail));
        item->on = on;
        item->enabled = enabled;
        const LONG mid = (item->rect.top + item->rect.bottom) / 2;
        item->control = {right - px(42), mid - px(11), right, mid + px(11)};
    };
    const auto choice = [&](SettingId id, const wchar_t* label, std::vector<std::wstring> options, int value,
                            bool enabled) {
        Item* item = add(Kind::Choice, id, px(50), label);
        item->options = std::move(options);
        item->value = value;
        item->enabled = enabled;
        const LONG segment = item->options.size() > 2 ? px(100) : px(128);
        const LONG left = right - segment * LONG(item->options.size());
        item->control = {left, item->rect.top + px(8), right, item->rect.bottom - px(8)};
        for (LONG i = 0; i < LONG(item->options.size()); ++i)
            item->parts.push_back({left + segment * i, item->control.top, left + segment * (i + 1), item->control.bottom});
    };
    const auto layouts = [&](SettingId id, std::wstring label, int value) {
        Item* item = add(Kind::Layouts, id, px(122), std::move(label));
        item->value = value;
        // Cards shrink to fit the page as layouts are added.
        const LONG gap = px(10);
        const LONG count = LONG(ZoneLayouts().size());
        const LONG card = std::min(px(98), (right - pad - gap * (count - 1)) / count);
        for (size_t i = 0; i < ZoneLayouts().size(); ++i) {
            const LONG left = pad + LONG(i) * (card + gap);
            item->parts.push_back({left, item->rect.top + px(32), left + card, item->rect.top + px(110)});
        }
    };

    switch (tab) {
        case 0: {
            section(L"Opening the void");
            Item* item = add(Kind::Hotkey, SettingId::Hotkey, px(58), L"Shortcut",
                             L"Opens the void from anywhere. Click the box to change it.");
            item->options = {HotkeyText(settings.hotkeyModifiers, settings.hotkeyKey)};
            const LONG mid = (item->rect.top + item->rect.bottom) / 2;
            item->control = {right - px(210), mid - px(16), right, mid + px(16)};
            section(L"Games");
            toggle(SettingId::PauseInFullscreen, L"Step aside for fullscreen games and videos",
                   L"Pauses the shortcut, the frame and snapping while something fills a screen.",
                   settings.pauseInFullscreen);
            break;
        }
        case 1: {
            section(L"Shell frame");
            toggle(SettingId::Frame, L"Show the frame",
                   L"A frame around the desktop with the brand, desktop name and quick buttons.", settings.frame);
            choice(SettingId::FrameStyle, L"Style", {L"Slim", L"Wide margin"}, settings.frameWide ? 1 : 0,
                   settings.frame);
            choice(SettingId::FrameMargin, L"Margin", {L"Small", L"Medium", L"Large"}, settings.frameMargin,
                   settings.frame && settings.frameWide);
            choice(SettingId::FrameMonitors, L"Show it on", {L"All monitors", L"Main monitor"},
                   settings.frameAllMonitors ? 0 : 1, settings.frame);
            section(L"Matrix rain");
            toggle(SettingId::Rain, L"Rain code through the margin",
                   L"Falls from every edge toward the desktop. Pauses when locked or on battery.", settings.matrixRain,
                   settings.frame && settings.frameWide);
            choice(SettingId::RainColor, L"Colour", {L"Venom", L"Green", L"Sheen"}, settings.rainColor,
                   settings.frame && settings.frameWide && settings.matrixRain);
            section(L"System info");
            toggle(SettingId::SystemStats, L"Show CPU, GPU and memory",
                   L"Use and temperatures in orbs at the top of the frame. Click them for Task Manager.",
                   settings.systemStats, settings.frame);
            section(L"Bubbles");
            toggle(SettingId::ShowSockets, L"Show empty sockets",
                   L"Where apps (sides) and folders (top and bottom) can go. They always show while you drag.",
                   settings.showSockets, settings.frame && settings.frameWide);
            break;
        }
        case 2: {
            section(L"Dock");
            toggle(SettingId::Dock, L"Show the dock",
                   settings.frame ? L"Your apps, desktops, volume and clock in the frame's bottom margin."
                                  : L"Lives in the frame's bottom margin, so it needs the frame.",
                   settings.dock, settings.frame);
            choice(SettingId::DockDesktops, L"Show apps from", {L"This desktop", L"All desktops"},
                   settings.dockAllDesktops ? 1 : 0, settings.frame && settings.dock);
            section(L"Windows taskbar");
            toggle(SettingId::HideTaskbar, L"Hide the Windows taskbar",
                   L"Auto-hides it while the dock is on. Put back when you turn this off or quit.",
                   settings.hideWindowsTaskbar, settings.frame && settings.dock);
            y += px(10);
            Item* item = add(Kind::Link, SettingId::ResetPins, px(36),
                             L"Pin the Windows taskbar's apps again (" + std::to_wstring(context.dockPins) + L" pinned now)");
            item->enabled = settings.frame && settings.dock;
            item->control = {pad, item->rect.top + px(4), pad + px(360), item->rect.bottom - px(8)};
            break;
        }
        case 3: {
            section(L"Snapping");
            toggle(SettingId::Snap, L"Snap windows when you drag them",
                   L"Hold Shift while dragging to place a window freely.", settings.snap);
            toggle(SettingId::AutoPlace, L"Place new windows in zones",
                   L"Newly opened app windows go into the first free zone.", settings.autoPlace);
            layouts(SettingId::DesktopLayout, L"Layout for " + context.desktopName, context.desktopLayout);
            layouts(SettingId::DefaultLayout, L"Layout for desktops without their own", settings.defaultLayout);
            Item* item = add(Kind::Link, SettingId::ResetLayouts, px(36), L"Give every desktop this layout");
            item->control = {pad, item->rect.top + px(4), pad + px(220), item->rect.bottom - px(8)};
            break;
        }
        default: {
            section(L"Startup");
            toggle(SettingId::StartAtLogin, L"Start when I sign in", L"", settings.startAtLogin);
            toggle(SettingId::RunAsAdmin, L"Run as administrator",
                   context.elevated ? L"Running as administrator now, so apps running as administrator snap too."
                                    : L"Lets snapping reach apps that run as administrator.",
                   settings.runAsAdmin);
            if (settings.runAsAdmin && !context.elevated) {
                Item* item = add(Kind::Button, SettingId::RestartElevated, px(50), L"Restart as administrator");
                item->control = {pad, item->rect.top + px(8), pad + px(230), item->rect.bottom - px(8)};
            }
            if (!context.startupNote.empty()) add(Kind::Note, SettingId::None, px(30), context.startupNote);
            break;
        }
    }
    return y;
}

SettingId SettingsView::HitTest(POINT point, int* part) const {
    *part = -1;
    if (PtInRect(&close_, point)) return SettingId::Close;
    for (size_t i = 0; i < tabs_.size(); ++i) {
        if (PtInRect(&tabs_[i], point)) {
            *part = int(i);
            return SettingId::Tab;
        }
    }
    for (const Item& item : items_) {
        if (!item.enabled) continue;
        switch (item.kind) {
            case Kind::Toggle:
                if (PtInRect(&item.rect, point)) return item.id;
                break;
            case Kind::Choice:
            case Kind::Layouts:
                for (size_t i = 0; i < item.parts.size(); ++i) {
                    if (PtInRect(&item.parts[i], point)) {
                        *part = int(i);
                        return item.id;
                    }
                }
                break;
            case Kind::Hotkey:
            case Kind::Button:
            case Kind::Link:
                if (PtInRect(&item.control, point)) return item.id;
                break;
            default:
                break;
        }
    }
    return SettingId::None;
}

void SettingsView::Paint(Canvas& canvas, SettingId hover, int hoverPart, bool recording,
                         const std::wstring& recordingText) const {
    const float s = scale_;
    const auto px = [s](float v) { return int(lroundf(v * s)); };
    const float width = float(width_);
    const float height = float(height_);
    const float radius = 14.0f * s;
    const float header = float(tabsBottom_);
    canvas.Clear();

    // Black glass body; the title and tab strip a shade lighter (opaque, so the two fills don't stack).
    canvas.FillRoundRect(0, 0, width, height, radius, venom::WithAlpha(venom::kVoid, 0.985f));
    canvas.FillRoundRect(0, 0, width, header, radius, venom::kHide);
    canvas.FillRoundRect(0, header * 0.5f, width, header, 0.0f, venom::kHide);
    canvas.FillRoundRect(0, header, width, header + std::max(1.0f, s), 0.0f, venom::kCardHi);
    // The rim runs crimson to blue, like the frame.
    canvas.StrokeRoundRect(0, 0, width, height, radius, 1.2f * s, venom::WithAlpha(venom::kFang, 0.55f));
    canvas.FillRoundRect(width * 0.5f, 0, width - radius, 1.2f * s, 0.0f, venom::WithAlpha(venom::kSheen, 0.55f));

    const float pad = 28.0f * s;
    const float cy = titleHeight_ * 0.5f;
    venom::DrawMark(canvas, pad + 11.0f * s, cy, 26.0f * s, venom::MarkStyle::Glow, true);
    const float nameWidth = canvas.DrawString(brand_, pad + 32.0f * s, cy, px(17), FW_BOLD, kText, venom::DisplayFont());
    canvas.DrawString(L"Void Shell settings", pad + 32.0f * s + nameWidth + 12.0f * s, cy + 1.0f * s, px(13),
                      FW_NORMAL, kDim, venom::UiFont());

    if (hover == SettingId::Close)
        canvas.FillRoundRect(float(close_.left), float(close_.top), float(close_.right), float(close_.bottom), 8.0f * s,
                             venom::WithAlpha(venom::kFang, 0.25f));
    const float xc = (close_.left + close_.right) * 0.5f;
    const float yc = Mid(close_);
    const float arm = 5.5f * s;
    canvas.DrawLine(xc - arm, yc - arm, xc + arm, yc + arm, 1.6f * s, kText);
    canvas.DrawLine(xc - arm, yc + arm, xc + arm, yc - arm, 1.6f * s, kText);

    for (size_t i = 0; i < tabs_.size(); ++i) {
        const RECT& t = tabs_[i];
        const bool active = int(i) == tab_;
        const bool hot = hover == SettingId::Tab && hoverPart == int(i);
        if (hot && !active) canvas.FillRoundRect(float(t.left), float(t.top), float(t.right), float(t.bottom), 8.0f * s, kHover);
        const float w = Canvas::MeasureString(kTabNames[i], px(13), FW_SEMIBOLD, venom::UiFont());
        canvas.DrawString(kTabNames[i], (t.left + t.right - w) * 0.5f, Mid(t), px(13), FW_SEMIBOLD,
                          active ? kText : kDim, venom::UiFont());
        if (active)
            canvas.FillRoundRect(t.left + 10.0f * s, t.bottom - 2.5f * s, t.right - 10.0f * s, float(t.bottom), 1.2f * s,
                                 venom::kFang);
    }

    for (const Item& item : items_) PaintItem(canvas, item, hover, hoverPart, recording, recordingText);
}

void SettingsView::PaintItem(Canvas& canvas, const Item& item, SettingId hover, int hoverPart, bool recording,
                             const std::wstring& recordingText) const {
    const float s = scale_;
    const auto px = [s](float v) { return int(lroundf(v * s)); };
    const float fade = item.enabled ? 1.0f : 0.4f;
    const bool hot = item.enabled && hover == item.id && item.id != SettingId::None;
    const RECT& r = item.rect;
    const RECT& c = item.control;
    const float textX = r.left + 10.0f * s;
    const wchar_t* ui = venom::UiFont();

    const auto text = [&](const std::wstring& t, float x, float y, int size, int weight, Rgba color) {
        return canvas.DrawString(t, x, y, size, weight, color, ui);
    };
    // A label, with an optional second line.
    const auto labels = [&](const RECT& row) {
        if (item.detail.empty()) {
            text(item.label, textX, Mid(row), px(14), FW_SEMIBOLD, Faded(kText, fade));
        } else {
            text(item.label, textX, row.top + 20.0f * s, px(14), FW_SEMIBOLD, Faded(kText, fade));
            text(item.detail, textX, row.top + 40.0f * s, px(12), FW_NORMAL, Faded(kDim, fade));
        }
    };
    const auto centered = [&](const std::wstring& t, const RECT& box, int size, int weight, Rgba color,
                              const wchar_t* family) {
        const float w = Canvas::MeasureString(t, size, weight, family);
        canvas.DrawString(t, (box.left + box.right - w) * 0.5f, Mid(box), size, weight, color, family);
    };

    switch (item.kind) {
        case Kind::Section: {
            const float y = r.top + 30.0f * s;
            const float w = canvas.DrawString(item.label, float(r.left + px(10)), y, px(11), FW_BOLD, kSection,
                                              venom::DisplayFont());
            canvas.FillRoundRect(r.left + px(10) + w + 12.0f * s, y, float(r.right - px(10)), y + std::max(1.0f, s), 0.0f,
                                 venom::kCardHi);
            break;
        }
        case Kind::Toggle: {
            if (hot) canvas.FillRoundRect(float(r.left), float(r.top), float(r.right), float(r.bottom), 8.0f * s, kHover);
            labels(r);
            const float h = float(c.bottom - c.top);
            if (item.on) {
                canvas.GlowRoundRect(float(c.left), float(c.top), float(c.right), float(c.bottom), h / 2, 4.0f * s,
                                     venom::WithAlpha(venom::kFang, 0.35f * fade));
                canvas.FillRoundRect(float(c.left), float(c.top), float(c.right), float(c.bottom), h / 2,
                                     venom::WithAlpha(venom::kFang, fade));
            } else {
                canvas.FillRoundRect(float(c.left), float(c.top), float(c.right), float(c.bottom), h / 2,
                                     venom::WithAlpha(venom::kCard, fade));
                canvas.StrokeRoundRect(float(c.left), float(c.top), float(c.right), float(c.bottom), h / 2, 1.0f * s,
                                       Faded(venom::kBoneFaint, fade));
            }
            const float knob = item.on ? c.right - h / 2 : c.left + h / 2;
            canvas.FillCircle(knob, Mid(c), h / 2 - 3.0f * s, Faded(kText, fade));
            break;
        }
        case Kind::Choice: {
            labels(r);
            canvas.FillRoundRect(float(c.left), float(c.top), float(c.right), float(c.bottom), 8.0f * s, Faded(kField, fade));
            canvas.StrokeRoundRect(float(c.left), float(c.top), float(c.right), float(c.bottom), 8.0f * s, 1.0f * s,
                                   Faded(kFieldEdge, fade));
            for (size_t i = 0; i < item.parts.size(); ++i) {
                const RECT& p = item.parts[i];
                const float inset = 3.0f * s;
                const bool selected = int(i) == item.value;
                if (selected) {
                    canvas.FillRoundRect(p.left + inset, p.top + inset, p.right - inset, p.bottom - inset, 6.0f * s,
                                         venom::WithAlpha(venom::kFang, 0.9f * fade));
                } else if (hot && hoverPart == int(i)) {
                    canvas.FillRoundRect(p.left + inset, p.top + inset, p.right - inset, p.bottom - inset, 6.0f * s,
                                         venom::WithAlpha(venom::kFang, 0.12f));
                }
                centered(item.options[i], p, px(13), selected ? FW_SEMIBOLD : FW_NORMAL,
                         Faded(selected ? kText : kDim, fade), ui);
            }
            break;
        }
        case Kind::Layouts: {
            text(item.label, textX, r.top + 16.0f * s, px(14), FW_SEMIBOLD, kText);
            for (size_t i = 0; i < item.parts.size(); ++i) {
                const RECT& p = item.parts[i];
                const bool selected = int(i) == item.value;
                const bool cardHot = hot && hoverPart == int(i);
                canvas.FillRoundRect(float(p.left), float(p.top), float(p.right), float(p.bottom), 9.0f * s,
                                     selected ? venom::WithAlpha(venom::kFang, 0.16f)
                                              : cardHot ? venom::WithAlpha(venom::kFang, 0.08f) : kField);
                canvas.StrokeRoundRect(float(p.left), float(p.top), float(p.right), float(p.bottom), 9.0f * s,
                                       selected ? 1.6f * s : 1.0f * s, selected ? venom::kFang : kFieldEdge);
                // A small picture of the layout's zones.
                const float gw = std::min(58.0f * s, float(p.right - p.left) - 20.0f * s);
                const float gh = 32.0f * s;
                const float gx = (p.left + p.right - gw) * 0.5f;
                const float gy = p.top + 10.0f * s;
                const Rgba zone = selected ? kText : venom::WithAlpha(venom::kBoneFaint, 1.0f);
                for (const auto& f : ZoneLayouts()[i].zones) {
                    canvas.FillRoundRect(gx + f[0] * gw + 1.5f * s, gy + f[1] * gh + 1.5f * s, gx + f[2] * gw - 1.5f * s,
                                         gy + f[3] * gh - 1.5f * s, 2.0f * s, zone);
                }
                const RECT name = {p.left, p.bottom - px(26), p.right, p.bottom - px(4)};
                centered(ZoneLayouts()[i].name, name, px(11.5f), FW_NORMAL, selected ? kText : kDim, ui);
            }
            break;
        }
        case Kind::Hotkey: {
            labels(r);
            canvas.FillRoundRect(float(c.left), float(c.top), float(c.right), float(c.bottom), 8.0f * s, kField);
            const Rgba edge = recording ? venom::kFang : hot ? venom::WithAlpha(venom::kFang, 0.6f) : kFieldEdge;
            canvas.StrokeRoundRect(float(c.left), float(c.top), float(c.right), float(c.bottom), 8.0f * s,
                                   recording ? 1.6f * s : 1.0f * s, edge);
            const std::wstring label =
                recording ? (recordingText.empty() ? L"Press a shortcut…" : recordingText) : item.options[0];
            centered(label, c, px(13), recording ? FW_NORMAL : FW_BOLD, recording ? venom::kFang : kText,
                     venom::MonoFont());
            break;
        }
        case Kind::Button: {
            const float h = float(c.bottom - c.top);
            canvas.FillRoundRect(float(c.left), float(c.top), float(c.right), float(c.bottom), h / 2,
                                 venom::WithAlpha(venom::kFang, hot ? 1.0f : 0.85f));
            centered(item.label, c, px(13), FW_BOLD, {1.0f, 1.0f, 1.0f, 1.0f}, ui);
            break;
        }
        case Kind::Link: {
            const Rgba color = Faded(kLink, (hot ? 1.0f : 0.75f) * fade);
            const float w = text(item.label, float(c.left), Mid(c), px(13), FW_SEMIBOLD, color);
            if (hot) canvas.FillRoundRect(float(c.left), Mid(c) + 9.0f * s, c.left + w, Mid(c) + 10.0f * s, 0.0f, color);
            break;
        }
        case Kind::Note:
            text(item.label, textX, Mid(r), px(12), FW_NORMAL, kWarn);
            break;
    }
}

bool SettingsWindow::Init(HINSTANCE instance, HICON icon, Callbacks callbacks) {
    instance_ = instance;
    callbacks_ = std::move(callbacks);
    WNDCLASSEXW windowClass = {sizeof(windowClass)};
    windowClass.lpfnWndProc = Proc;
    windowClass.hInstance = instance;
    windowClass.hIcon = icon;
    windowClass.hIconSm = icon;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = kWindowClass;
    return RegisterClassExW(&windowClass) != 0;
}

void SettingsWindow::Show(const Settings& settings, const SettingsContext& context) {
    settings_ = settings;
    context_ = context;
    if (window_) {
        Rebuild();
        ShowWindow(window_, SW_RESTORE);
        SetForegroundWindow(window_);
        return;
    }
    POINT cursor;
    GetCursorPos(&cursor);
    const HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    scale_ = MonitorScale(monitor);
    view_.Build(settings_, context_, scale_, tab_);
    const RECT work = WorkArea(monitor);
    // On a short screen (or with large display scaling) shrink the page until it fits.
    const int room = int(work.bottom - work.top) - 40;
    if (view_.Height() > room && room > 200) {
        scale_ *= float(room) / float(view_.Height());
        view_.Build(settings_, context_, scale_, tab_);
    }
    const int x = (work.left + work.right - view_.Width()) / 2;
    const int y = std::max(int(work.top) + 20, int(work.top + work.bottom - view_.Height()) / 2);
    const std::wstring title = (settings_.brandName.empty() ? std::wstring(L"VenomMind") : settings_.brandName) +
                               L" settings";
    window_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_APPWINDOW, kWindowClass, title.c_str(),
                              WS_POPUP | WS_SYSMENU | WS_MINIMIZEBOX, x, y, view_.Width(), view_.Height(), nullptr,
                              nullptr, instance_, this);
    if (!window_) return;
    Repaint();
    ShowWindow(window_, SW_SHOW);
    SetForegroundWindow(window_);
}

void SettingsWindow::ShowTab(int tab, const Settings& settings, const SettingsContext& context) {
    tab_ = std::clamp(tab, 0, kSettingsTabs - 1);
    Show(settings, context);
}

void SettingsWindow::Update(const Settings& settings, const SettingsContext& context) {
    settings_ = settings;
    context_ = context;
    if (window_) Rebuild();
}

void SettingsWindow::Close() {
    if (recording_) StopRecording();
    if (!window_) return;
    const HWND window = window_;
    window_ = nullptr;
    DestroyWindow(window);
}

void SettingsWindow::Rebuild() {
    view_.Build(settings_, context_, scale_, tab_);
    Repaint();
}

void SettingsWindow::Repaint() {
    if (!window_ || !canvas_.Resize(view_.Width(), view_.Height())) return;
    view_.Paint(canvas_, hover_, hoverPart_, recording_, recordingText_);
    RECT rect;
    GetWindowRect(window_, &rect);
    canvas_.Present(window_, {rect.left, rect.top});
}

void SettingsWindow::Activate(SettingId id, int part) {
    switch (id) {
        case SettingId::None:
            return;
        case SettingId::Close:
            Close();
            return;
        case SettingId::Tab:
            if (recording_) StopRecording();
            tab_ = part;
            Rebuild();
            return;
        case SettingId::Hotkey:
            if (recording_) {
                StopRecording();
                return;
            }
            recording_ = true;
            recordingText_.clear();
            if (callbacks_.recording) callbacks_.recording(true);
            Repaint();
            return;
        default:
            if (callbacks_.changed) callbacks_.changed(id, part);
    }
}

void SettingsWindow::StopRecording() {
    recording_ = false;
    recordingText_.clear();
    if (callbacks_.recording) callbacks_.recording(false);
    Repaint();
}

void SettingsWindow::OnKey(UINT key) {
    if (key == VK_ESCAPE) {
        StopRecording();
        return;
    }
    switch (key) {
        case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL: case VK_MENU: case VK_LMENU: case VK_RMENU:
        case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT: case VK_LWIN: case VK_RWIN:
            return;  // wait for the key that goes with them
    }
    UINT modifiers = 0;
    if (GetKeyState(VK_CONTROL) & 0x8000) modifiers |= MOD_CONTROL;
    if (GetKeyState(VK_MENU) & 0x8000) modifiers |= MOD_ALT;
    if (GetKeyState(VK_SHIFT) & 0x8000) modifiers |= MOD_SHIFT;
    if ((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000) modifiers |= MOD_WIN;
    if (key == VK_BACK && modifiers == 0) {  // Backspace puts the default back
        modifiers = MOD_CONTROL | MOD_ALT;
        key = VK_SPACE;
    }
    if (!(modifiers & (MOD_CONTROL | MOD_ALT | MOD_WIN))) {
        recordingText_ = L"Hold Ctrl, Alt or Win too";
        Repaint();
        return;
    }
    if (!callbacks_.setHotkey || !callbacks_.setHotkey(modifiers, key)) {
        recordingText_ = HotkeyText(modifiers, key) + L" is taken";
        Repaint();
        return;
    }
    StopRecording();
}

LRESULT CALLBACK SettingsWindow::Proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* self = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    return self ? self->Handle(window, message, wParam, lParam) : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT SettingsWindow::Handle(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_NCHITTEST: {
            POINT point = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ScreenToClient(window, &point);
            int part = -1;
            // The title bar drags the window, except over the close button.
            if (view_.InTitleBar(point) && view_.HitTest(point, &part) == SettingId::None) return HTCAPTION;
            return HTCLIENT;
        }
        case WM_MOUSEMOVE: {
            int part = -1;
            const SettingId id = view_.HitTest({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}, &part);
            if (id != hover_ || part != hoverPart_) {
                hover_ = id;
                hoverPart_ = part;
                Repaint();
            }
            TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, window, 0};
            TrackMouseEvent(&track);
            return 0;
        }
        case WM_MOUSELEAVE:
            if (hover_ != SettingId::None) {
                hover_ = SettingId::None;
                hoverPart_ = -1;
                Repaint();
            }
            return 0;
        case WM_LBUTTONUP: {
            int part = -1;
            const SettingId id = view_.HitTest({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}, &part);
            Activate(id, part);
            return 0;
        }
        case WM_SETCURSOR:
            if (LOWORD(lParam) == HTCLIENT) {
                SetCursor(LoadCursorW(nullptr, hover_ != SettingId::None ? IDC_HAND : IDC_ARROW));
                return TRUE;
            }
            break;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            if (recording_) {
                OnKey(UINT(wParam));
                return 0;
            }
            if (message == WM_KEYDOWN && wParam == VK_ESCAPE) {
                Close();
                return 0;
            }
            if (message == WM_KEYDOWN && wParam == VK_TAB) {  // Tab / Shift+Tab move between tabs
                tab_ = (tab_ + ((GetKeyState(VK_SHIFT) & 0x8000) ? kSettingsTabs - 1 : 1)) % kSettingsTabs;
                Rebuild();
                return 0;
            }
            break;
        case WM_SYSCHAR:
            if (recording_) return 0;  // no beep for Alt+key while recording
            break;
        case WM_ACTIVATE:
            if (LOWORD(wParam) == WA_INACTIVE && recording_) StopRecording();
            return 0;
        case WM_DPICHANGED: {
            scale_ = HIWORD(wParam) / 96.0f;
            const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
            view_.Build(settings_, context_, scale_, tab_);
            SetWindowPos(window, nullptr, suggested->left, suggested->top, view_.Width(), view_.Height(),
                         SWP_NOZORDER | SWP_NOACTIVATE);
            Repaint();
            return 0;
        }
        case WM_CLOSE:
            Close();
            return 0;
        case WM_DESTROY:
            if (window == window_) window_ = nullptr;
            return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
