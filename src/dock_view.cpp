#include "dock.h"

#include <algorithm>
#include <cmath>

#include "brand.h"

namespace {

// Segoe Fluent Icons / MDL2 code points, as the Windows taskbar uses them.
constexpr wchar_t kGlyphSearch = 0xE721;
constexpr wchar_t kGlyphMute = 0xE74F;
constexpr wchar_t kGlyphVolume0 = 0xE992;
constexpr wchar_t kGlyphVolume1 = 0xE993;
constexpr wchar_t kGlyphVolume2 = 0xE994;
constexpr wchar_t kGlyphVolume3 = 0xE995;
constexpr wchar_t kGlyphClose = 0xE8BB;

constexpr float kPill = 54.0f;     // pill height
constexpr float kGlow = 8.0f;      // room around the pill for its glow
constexpr float kPad = 8.0f;
constexpr float kButton = 40.0f;   // Start, search, volume
constexpr float kCell = 46.0f;     // one app
constexpr float kIcon = 30.0f;
constexpr float kDesktop = 18.0f;  // one desktop dot
constexpr float kRadius = 18.0f;

float Clamp01(float x) { return std::clamp(x, 0.0f, 1.0f); }

uint32_t PackPremultiplied(float a, float r, float g, float b) {
    const auto channel = [](float v) { return uint32_t(Clamp01(v) * 255.0f + 0.5f); };
    return (channel(a) << 24) | (channel(r * a) << 16) | (channel(g * a) << 8) | channel(b * a);
}

Rgba Mix(Rgba a, Rgba b, float t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}

float Mid(LONG a, LONG b) { return (a + b) * 0.5f; }

void Fill(Canvas& canvas, const RECT& r, float radius, Rgba color) {
    canvas.FillRoundRect(float(r.left), float(r.top), float(r.right), float(r.bottom), radius, color);
}

RECT Inset(const RECT& r, LONG dx, LONG dy) { return {r.left + dx, r.top + dy, r.right - dx, r.bottom - dy}; }

// Text centred on a point.
void Centered(Canvas& canvas, const std::wstring& text, float cx, float cy, int size, int weight, Rgba color,
              const wchar_t* family) {
    const float width = Canvas::MeasureString(text, size, weight, family);
    canvas.DrawString(text, cx - width * 0.5f, cy, size, weight, color, family);
}

// Cuts text to fit a width, ending it with an ellipsis.
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

// Venom glass: near-black, a touch lighter at the top, with a rim running crimson to blue like the frame.
void PaintGlass(Canvas& canvas, const RECT& pill, float radius, float s) {
    uint32_t* pixels = canvas.Pixels();
    const float left = float(pill.left), top = float(pill.top), right = float(pill.right), bottom = float(pill.bottom);
    const float width = std::max(1.0f, right - left);
    for (int y = std::max(0, int(top) - 1); y < std::min(canvas.Height(), int(bottom) + 1); ++y) {
        const float py = y + 0.5f;
        const float v = Clamp01((py - top) / std::max(1.0f, bottom - top));
        for (int x = std::max(0, int(left) - 1); x < std::min(canvas.Width(), int(right) + 1); ++x) {
            const float px = x + 0.5f;
            const float d = SdRoundRect(px, py, left, top, right, bottom, radius);
            const float coverage = Clamp01(0.5f - d);
            if (coverage <= 0.0f) continue;
            const float t = Clamp01((px - left) / width);
            const Rgba rim = Mix(venom::kFang, venom::kSheen, t);
            const Rgba base = Mix(venom::kCard, venom::kVoid, v);
            const float line = Clamp01(1.0f - fabsf(d + 0.9f * s) / (0.9f * s));
            const float inner = expf(-std::max(-d, 0.0f) / (5.0f * s)) * 0.10f;  // the rim's light inside the glass
            float r = base.r + rim.r * inner, g = base.g + rim.g * inner, b = base.b + rim.b * inner;
            r += (rim.r - r) * line * 0.85f;
            g += (rim.g - g) * line * 0.85f;
            b += (rim.b - b) * line * 0.85f;
            const uint32_t over = PackPremultiplied(coverage * 0.96f, r, g, b);
            // The glow behind the pill is already in the canvas; put the glass over it.
            uint32_t& pixel = pixels[size_t(y) * canvas.Width() + x];
            const uint32_t sa = over >> 24, inverse = 255 - sa;
            const auto mix = [&](int shift) {
                return std::min(255u, ((over >> shift) & 255) + (((pixel >> shift) & 255) * inverse + 127) / 255);
            };
            pixel = (mix(24) << 24) | (mix(16) << 16) | (mix(8) << 8) | mix(0);
        }
    }
}

void PaintStart(Canvas& canvas, const RECT& r, bool hot, float s) {
    const float cx = Mid(r.left, r.right), cy = Mid(r.top, r.bottom);
    const float q = 7.5f * s, gap = 2.0f * s;
    if (hot) canvas.GlowRoundRect(cx - q - gap / 2, cy - q - gap / 2, cx + q + gap / 2, cy + q + gap / 2, 3.0f * s,
                                  3.5f * s, venom::WithAlpha(venom::kFang, 0.45f));
    const Rgba colors[4] = {venom::kFang, Mix(venom::kFang, venom::kSheen, 0.35f), Mix(venom::kFang, venom::kSheen, 0.65f),
                            venom::kSheen};
    for (int i = 0; i < 4; ++i) {
        const float x = i % 2 ? cx + gap / 2 : cx - gap / 2 - q;
        const float y = i / 2 ? cy + gap / 2 : cy - gap / 2 - q;
        canvas.FillRoundRect(x, y, x + q, y + q, 2.0f * s, hot ? colors[i] : venom::WithAlpha(colors[i], 0.88f));
    }
}

void PaintLetterTile(Canvas& canvas, const std::wstring& name, float cx, float cy, float size, float s) {
    const float h = size * 0.5f;
    canvas.FillRoundRect(cx - h, cy - h, cx + h, cy + h, 7.0f * s, venom::kCardHi);
    canvas.StrokeRoundRect(cx - h, cy - h, cx + h, cy + h, 7.0f * s, 1.0f * s, venom::WithAlpha(venom::kFang, 0.6f));
    std::wstring letter = name.empty() ? L"?" : name.substr(0, 1);
    CharUpperBuffW(letter.data(), DWORD(letter.size()));
    Centered(canvas, letter, cx, cy, int(lroundf(size * 0.5f)), FW_BOLD, venom::kBone, venom::DisplayFont());
}

wchar_t VolumeGlyph(const DockState& state) {
    if (state.muted) return kGlyphMute;
    if (!state.hasAudio || state.volume <= 0.001f) return kGlyphVolume0;
    if (state.volume < 0.34f) return kGlyphVolume1;
    if (state.volume < 0.67f) return kGlyphVolume2;
    return kGlyphVolume3;
}

}  // namespace

int DockBandHeight(float scale) { return int(lroundf((kPill + 2.0f * kGlow) * scale)); }

int DockClockWidth(float scale) {
    // A late, wide time and a wide date in this locale's own formats.
    SYSTEMTIME sample = {};
    GetLocalTime(&sample);
    sample.wMonth = 12;
    sample.wDay = 28;
    sample.wHour = 22;
    sample.wMinute = 58;
    wchar_t time[64] = {}, date[64] = {};
    GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &sample, nullptr, time, ARRAYSIZE(time));
    GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, &sample, nullptr, date, ARRAYSIZE(date), nullptr);
    const float width = std::max(Canvas::MeasureString(time, int(lroundf(13.0f * scale)), FW_SEMIBOLD, venom::UiFont()),
                                 Canvas::MeasureString(date, int(lroundf(11.0f * scale)), FW_NORMAL, venom::UiFont()));
    return int(ceilf(width + 20.0f * scale));
}

DockLayout LayoutDock(int apps, int desktops, float scale, int maxWidth, int clockWidth) {
    const float s = scale;
    const auto px = [s](float v) { return LONG(lroundf(v * s)); };
    DockLayout layout;
    layout.scale = s;
    apps = std::max(0, apps);
    desktops = std::max(1, desktops);

    const LONG glow = px(kGlow), pad = px(kPad), button = px(kButton), gap = px(2), sep = px(6), group = px(4);
    const LONG desktopCell = px(kDesktop);
    // Everything but the apps, so a crowded dock can squeeze the apps to fit.
    const LONG fixed = glow * 2 + pad * 2 + button * 2 + gap + sep * 2 * (apps > 0 ? 3 : 2) + group * 2 +
                       desktopCell * desktops + button + gap + clockWidth;
    LONG cell = px(kCell);
    if (apps > 0 && maxWidth > 0 && fixed + cell * apps > maxWidth)
        cell = std::max(px(28), (maxWidth - fixed) / LONG(apps));
    layout.iconSize = std::min(int(px(kIcon)), int(cell - px(12)));

    const LONG top = glow, bottom = glow + px(kPill);
    const LONG cy = (top + bottom) / 2;
    const auto box = [&](LONG left, LONG width, LONG height) {
        return RECT{left, cy - height / 2, left + width, cy - height / 2 + height};
    };
    LONG x = glow + pad;
    layout.start = box(x, button, button);
    x += button + gap;
    layout.search = box(x, button, button);
    x += button;
    const auto separator = [&] {
        x += sep;
        layout.separators.push_back(x);
        x += sep;
    };
    separator();
    for (int i = 0; i < apps; ++i) {
        layout.apps.push_back(box(x, cell, px(kCell)));
        x += cell;
    }
    if (apps > 0) separator();
    x += group;
    for (int i = 0; i < desktops; ++i) {
        layout.desktops.push_back(box(x, desktopCell, button));
        x += desktopCell;
    }
    x += group;
    separator();
    layout.volume = box(x, button, button);
    x += button + gap;
    layout.clock = box(x, clockWidth, button);
    x += clockWidth + pad;
    layout.pill = {glow, top, x, bottom};
    layout.width = x + glow;
    layout.height = bottom + glow;
    return layout;
}

RECT DockPartRect(const DockLayout& layout, DockHit hit) {
    switch (hit.part) {
        case DockPart::Start: return layout.start;
        case DockPart::Search: return layout.search;
        case DockPart::App: return hit.index >= 0 && hit.index < int(layout.apps.size()) ? layout.apps[hit.index] : RECT{};
        case DockPart::Desktop:
            return hit.index >= 0 && hit.index < int(layout.desktops.size()) ? layout.desktops[hit.index] : RECT{};
        case DockPart::Volume: return layout.volume;
        case DockPart::Clock: return layout.clock;
        default: return {};
    }
}

DockHit DockHitTest(const DockLayout& layout, POINT p) {
    // Anywhere in the pill's height counts, so the dock is easy to hit from the screen edge.
    if (p.y < layout.pill.top || p.y >= layout.pill.bottom || p.x < layout.pill.left || p.x >= layout.pill.right)
        return {};
    const auto within = [&](const RECT& r) { return p.x >= r.left && p.x < r.right; };
    if (within(layout.start)) return {DockPart::Start, -1};
    if (within(layout.search)) return {DockPart::Search, -1};
    for (size_t i = 0; i < layout.apps.size(); ++i)
        if (within(layout.apps[i])) return {DockPart::App, int(i)};
    for (size_t i = 0; i < layout.desktops.size(); ++i)
        if (within(layout.desktops[i])) return {DockPart::Desktop, int(i)};
    if (within(layout.volume)) return {DockPart::Volume, -1};
    if (within(layout.clock)) return {DockPart::Clock, -1};
    return {};
}

void PaintDock(Canvas& canvas, const DockLayout& layout, const std::vector<DockApp>& apps,
               const std::vector<const IconImage*>& icons, const DockState& state) {
    const float s = layout.scale;
    const auto px = [s](float v) { return int(lroundf(v * s)); };
    canvas.Clear();
    const RECT& pill = layout.pill;
    const float radius = kRadius * s;
    canvas.GlowRoundRect(float(pill.left), float(pill.top), float(pill.right), float(pill.bottom), radius, 4.0f * s,
                         venom::WithAlpha(venom::kFang, 0.22f));
    PaintGlass(canvas, pill, radius, s);

    const auto hot = [&](DockPart part, int index = -1) {
        return state.hover.part == part && state.hover.index == index;
    };
    const auto hoverPlate = [&](const RECT& r) { Fill(canvas, Inset(r, px(2), px(2)), 10.0f * s, venom::WithAlpha(venom::kCardHi, 0.95f)); };
    const wchar_t* glyphFont = venom::IconFont();

    // Start and search.
    if (hot(DockPart::Start)) hoverPlate(layout.start);
    PaintStart(canvas, layout.start, hot(DockPart::Start), s);
    if (hot(DockPart::Search)) hoverPlate(layout.search);
    Centered(canvas, std::wstring(1, kGlyphSearch), Mid(layout.search.left, layout.search.right),
             Mid(layout.search.top, layout.search.bottom), px(16), FW_NORMAL,
             hot(DockPart::Search) ? venom::kBone : venom::kBoneDim, glyphFont);

    // Apps.
    for (size_t i = 0; i < layout.apps.size() && i < apps.size(); ++i) {
        const RECT& r = layout.apps[i];
        const DockApp& app = apps[i];
        const bool isHot = hot(DockPart::App, int(i));
        if (app.attention) {
            Fill(canvas, Inset(r, px(2), px(2)), 10.0f * s, venom::WithAlpha(venom::kFang, 0.12f + 0.26f * state.pulse));
        } else if (isHot) {
            hoverPlate(r);
        } else if (app.active) {
            Fill(canvas, Inset(r, px(2), px(2)), 10.0f * s, venom::WithAlpha(venom::kCardHi, 0.7f));
        }
        const float cx = Mid(r.left, r.right);
        const float cy = Mid(r.top, r.bottom) - 2.0f * s - (isHot ? 2.0f * s : 0.0f);
        const IconImage* icon = i < icons.size() ? icons[i] : nullptr;
        if (icon && !icon->Empty()) {
            const int size = std::min(icon->size, layout.iconSize);
            const int left = int(lroundf(cx - size * 0.5f)), top = int(lroundf(cy - size * 0.5f));
            canvas.DrawPixels(icon->pixels.data(), icon->size, icon->size, left, top, 1.0f);
        } else {
            PaintLetterTile(canvas, app.name, cx, cy, float(layout.iconSize), s);
        }
        // Under the icon: a short line while it runs, a long crimson one when it has the focus.
        if (!app.windows.empty()) {
            const float y = float(r.bottom) - 4.5f * s;
            const float half = (app.active || app.attention ? 8.0f : (app.windows.size() > 1 ? 5.0f : 3.0f)) * s;
            const Rgba color = app.active || app.attention ? venom::kFang : venom::kBoneDim;
            if (app.active || app.attention)
                canvas.GlowRoundRect(cx - half, y - 1.5f * s, cx + half, y + 1.5f * s, 1.5f * s, 2.5f * s,
                                     venom::WithAlpha(venom::kFang, 0.55f));
            canvas.FillRoundRect(cx - half, y - 1.5f * s, cx + half, y + 1.5f * s, 1.5f * s, color);
        }
    }

    for (const LONG x : layout.separators) {
        const float cy = Mid(pill.top, pill.bottom);
        canvas.FillRoundRect(float(x), cy - 13.0f * s, float(x) + std::max(1.0f, s), cy + 13.0f * s, 0.0f,
                             venom::WithAlpha(venom::kBoneFaint, 0.45f));
    }

    // Desktops: the current one is a glowing blue bar.
    for (size_t i = 0; i < layout.desktops.size(); ++i) {
        const RECT& r = layout.desktops[i];
        const float cx = Mid(r.left, r.right), cy = Mid(r.top, r.bottom);
        const bool isHot = hot(DockPart::Desktop, int(i));
        if (int(i) == state.currentDesktop) {
            canvas.GlowRoundRect(cx - 6.0f * s, cy - 3.0f * s, cx + 6.0f * s, cy + 3.0f * s, 3.0f * s, 3.0f * s,
                                 venom::WithAlpha(venom::kSheen, 0.6f));
            canvas.FillRoundRect(cx - 6.0f * s, cy - 3.0f * s, cx + 6.0f * s, cy + 3.0f * s, 3.0f * s, venom::kSheen);
        } else {
            canvas.FillCircle(cx, cy, (isHot ? 3.5f : 3.0f) * s, isHot ? venom::kBone : venom::kBoneFaint);
        }
    }

    // Volume: the glyph and a thin level bar.
    {
        const RECT& r = layout.volume;
        if (hot(DockPart::Volume)) hoverPlate(r);
        const float cx = Mid(r.left, r.right), cy = Mid(r.top, r.bottom);
        const Rgba color = state.muted ? venom::kFang : (hot(DockPart::Volume) ? venom::kBone : venom::kBoneDim);
        Centered(canvas, std::wstring(1, VolumeGlyph(state)), cx, cy - 3.0f * s, px(16), FW_NORMAL,
                 state.hasAudio ? color : venom::kBoneFaint, glyphFont);
        if (state.hasAudio) {
            const float left = cx - 10.0f * s, right = cx + 10.0f * s, y = cy + 10.0f * s;
            canvas.FillRoundRect(left, y, right, y + 2.0f * s, 1.0f * s, venom::WithAlpha(venom::kBoneFaint, 0.5f));
            if (!state.muted && state.volume > 0.0f)
                canvas.FillRoundRect(left, y, left + (right - left) * Clamp01(state.volume), y + 2.0f * s, 1.0f * s,
                                     venom::kSheen);
        }
    }

    // The clock opens notifications and the calendar, as on the Windows taskbar.
    {
        const RECT& r = layout.clock;
        if (hot(DockPart::Clock)) hoverPlate(r);
        const float cx = Mid(r.left, r.right), cy = Mid(r.top, r.bottom);
        Centered(canvas, state.time, cx, cy - 7.0f * s, px(13), FW_SEMIBOLD, venom::kBone, venom::UiFont());
        Centered(canvas, state.date, cx, cy + 9.0f * s, px(11), FW_NORMAL, venom::kBoneDim, venom::UiFont());
    }
}

PreviewLayout LayoutPreview(const DockApp& app, float scale) {
    const float s = scale;
    const auto px = [s](float v) { return LONG(lroundf(v * s)); };
    PreviewLayout layout;
    const int count = std::min(int(app.windows.size()), kMaxPreviewWindows);
    if (count == 0) {
        // A label.
        const float width = Canvas::MeasureString(app.name, int(px(12)), FW_SEMIBOLD, venom::UiFont());
        layout.width = int(ceilf(width)) + px(24);
        layout.height = px(32);
        return layout;
    }
    const LONG pad = px(8), gap = px(6), cellWidth = px(220), cellHeight = px(26 + 124 + 8);
    for (int i = 0; i < count; ++i) {
        const LONG left = pad + i * (cellWidth + gap);
        const RECT cell = {left, pad, left + cellWidth, pad + cellHeight};
        layout.cells.push_back(cell);
        layout.thumbs.push_back({cell.left + px(8), cell.top + px(26), cell.right - px(8), cell.bottom - px(8)});
        layout.closes.push_back({cell.right - px(28), cell.top + px(3), cell.right - px(4), cell.top + px(23)});
    }
    layout.width = pad * 2 + count * cellWidth + (count - 1) * gap;
    layout.height = pad * 2 + cellHeight;
    return layout;
}

int PreviewCellAt(const PreviewLayout& layout, POINT point, bool* onClose) {
    *onClose = false;
    for (size_t i = 0; i < layout.cells.size(); ++i) {
        if (!PtInRect(&layout.cells[i], point)) continue;
        *onClose = PtInRect(&layout.closes[i], point) != FALSE;
        return int(i);
    }
    return -1;
}

void PaintPreview(Canvas& canvas, const PreviewLayout& layout, const DockApp& app, const IconImage* icon,
                  const std::vector<bool>& live, int hoverCell, bool hoverClose, float scale) {
    const float s = scale;
    const auto px = [s](float v) { return int(lroundf(v * s)); };
    const float width = float(canvas.Width()), height = float(canvas.Height());
    // Opaque: this window isn't layered, so DWM can draw live thumbnails into it. DWM rounds its corners.
    canvas.FillRoundRect(0, 0, width, height, 0.0f, venom::kHide);
    canvas.FillRoundRect(0, 0, width, std::max(1.0f, s), 0.0f, venom::WithAlpha(venom::kFang, 0.8f));
    canvas.FillRoundRect(width * 0.5f, 0, width, std::max(1.0f, s), 0.0f, venom::WithAlpha(venom::kSheen, 0.8f));
    if (layout.cells.empty()) {
        Centered(canvas, app.name, width * 0.5f, height * 0.5f, px(12), FW_SEMIBOLD, venom::kBone, venom::UiFont());
        return;
    }
    for (size_t i = 0; i < layout.cells.size() && i < app.windows.size(); ++i) {
        const RECT& cell = layout.cells[i];
        const bool isHot = int(i) == hoverCell;
        if (isHot) Fill(canvas, cell, 8.0f * s, venom::kCardHi);
        const RECT& close = layout.closes[i];
        const float titleRight = float(isHot ? close.left - px(4) : cell.right - px(8));
        const std::wstring title = Ellipsize(app.windows[i].title, titleRight - (cell.left + px(8)), px(12),
                                             FW_SEMIBOLD, venom::UiFont());
        canvas.DrawString(title, float(cell.left + px(8)), float(cell.top + px(13)), px(12), FW_SEMIBOLD,
                          isHot ? venom::kBone : venom::kBoneDim, venom::UiFont());
        if (isHot) {
            if (hoverClose) Fill(canvas, close, 5.0f * s, venom::kFang);
            Centered(canvas, std::wstring(1, kGlyphClose), Mid(close.left, close.right), Mid(close.top, close.bottom),
                     px(10), FW_NORMAL, venom::kBone, venom::IconFont());
        }
        const RECT& thumb = layout.thumbs[i];
        Fill(canvas, thumb, 6.0f * s, venom::kVoid);
        if (i >= live.size() || !live[i]) {
            // No live picture (minimized to nothing, or DWM refused): the app's icon instead.
            const float cx = Mid(thumb.left, thumb.right), cy = Mid(thumb.top, thumb.bottom);
            if (icon && !icon->Empty()) {
                canvas.DrawPixels(icon->pixels.data(), icon->size, icon->size, int(cx - icon->size * 0.5f),
                                  int(cy - icon->size * 0.5f), 0.9f);
            } else {
                PaintLetterTile(canvas, app.name, cx, cy, 32.0f * s, s);
            }
        }
    }
}
