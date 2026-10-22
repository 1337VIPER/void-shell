#include "frame.h"

#include <windowsx.h>

#include <algorithm>
#include <climits>
#include <cmath>

#include "brand.h"
#include "bubbles.h"
#include "stat_orbs.h"

namespace {

constexpr wchar_t kBarClass[] = L"VoidShellFrameBar";
constexpr UINT kAppBarMessage = WM_APP + 20;
constexpr UINT_PTR kNoTimer = 0;
constexpr UINT kRainFrameMs = 50;  // 20 steps a second: the code moves a cell at a time anyway

ShellFrame* g_frame = nullptr;

float Clamp01(float x) { return std::clamp(x, 0.0f, 1.0f); }

uint32_t PackPremultiplied(float a, float r, float g, float b) {
    const auto channel = [](float v) { return uint32_t(Clamp01(v) * 255.0f + 0.5f); };
    return (channel(a) << 24) | (channel(r * a) << 16) | (channel(g * a) << 8) | channel(b * a);
}

// Positions on the top strip, shared by painting and hit-testing. Bar-local pixels.
struct TopBarLayout {
    float centerY;
    float markX;
    float markSize;
    float textX;
    RECT voidButton;
    RECT settingsButton;
    float gearX;
    RECT addButton;  // wide margin only: add a folder or app bubble
    float addX;
    RECT layoutButton;
    float glyphLeft, glyphTop, glyphRight, glyphBottom;
};

TopBarLayout ComputeTopBar(const FrameGeometry& g, int barWidth) {
    const float s = g.scale;
    TopBarLayout t = {};
    t.centerY = g.top - g.strip * 0.5f;
    t.markSize = std::min(g.strip * 0.78f, 24.0f * s);
    t.markX = g.side + 8.0f * s + t.markSize * 0.5f;
    t.textX = t.markX + t.markSize * 0.5f + 8.0f * s;
    t.voidButton = {LONG(g.side + 2.0f * s), LONG(t.centerY - 12.0f * s), LONG(t.textX + 230.0f * s),
                    LONG(t.centerY + 12.0f * s)};
    t.glyphRight = barWidth - g.side - 18.0f * s;
    t.glyphLeft = t.glyphRight - 24.0f * s;
    t.glyphTop = t.centerY - 7.0f * s;
    t.glyphBottom = t.centerY + 7.0f * s;
    t.layoutButton = {LONG(t.glyphLeft - 8.0f * s), LONG(t.centerY - 12.0f * s), LONG(t.glyphRight + 8.0f * s),
                      LONG(t.centerY + 12.0f * s)};
    t.gearX = t.layoutButton.left - 18.0f * s;
    t.settingsButton = {LONG(t.gearX - 13.0f * s), LONG(t.centerY - 12.0f * s), LONG(t.gearX + 13.0f * s),
                        LONG(t.centerY + 12.0f * s)};
    t.addX = t.settingsButton.left - 15.0f * s;
    t.addButton = {LONG(t.addX - 13.0f * s), LONG(t.centerY - 12.0f * s), LONG(t.addX + 13.0f * s),
                   LONG(t.centerY + 12.0f * s)};
    return t;
}

void DrawGear(Canvas& canvas, float cx, float cy, float s, Rgba color) {
    const float radius = 5.0f * s;
    for (int i = 0; i < 8; ++i) {
        const float angle = float(i) * 0.7853982f;
        canvas.FillCircle(cx + cosf(angle) * radius, cy + sinf(angle) * radius, 1.7f * s, color);
    }
    canvas.FillCircle(cx, cy, radius, color);
    canvas.FillCircle(cx, cy, 2.0f * s, venom::kHide);  // the hole, in the strip's colour
}

void ApplyThickness(RECT& rect, UINT edge, int thickness) {
    switch (edge) {
        case ABE_TOP: rect.bottom = rect.top + thickness; break;
        case ABE_BOTTOM: rect.top = rect.bottom - thickness; break;
        case ABE_LEFT: rect.right = rect.left + thickness; break;
        case ABE_RIGHT: rect.left = rect.right - thickness; break;
    }
}

}  // namespace

FrameGeometry FrameSizes(const RECT& monitor, float scale, const FrameStyle& style) {
    FrameGeometry g = {};
    g.monitor = monitor;
    g.inner = monitor;
    g.scale = scale;
    g.wide = style.wide;
    if (style.wide) {
        const float margins[] = {56.0f, 96.0f, 150.0f};
        const int margin = int(lroundf(margins[std::clamp(style.margin, 0, 2)] * scale));
        g.strip = int(lroundf(34.0f * scale));
        g.top = margin + g.strip;
        g.side = margin;
        g.bottom = margin;
        g.radius = int(lroundf(18.0f * scale));
    } else {
        g.top = int(lroundf(30.0f * scale));
        g.strip = g.top;
        g.side = std::max(4, int(lroundf(7.0f * scale)));
        g.bottom = g.side;
        g.radius = int(lroundf(14.0f * scale));
    }
    return g;
}

void FrameBarRects(const FrameGeometry& g, RECT rects[4]) {
    const RECT& in = g.inner;
    rects[0] = {in.left - g.side, in.top - g.top, in.right + g.side, in.top + g.radius};
    rects[1] = {in.left - g.side, in.top + g.radius, in.left, in.bottom - g.radius};
    rects[2] = {in.right, in.top + g.radius, in.right + g.side, in.bottom - g.radius};
    rects[3] = {in.left - g.side, in.bottom - g.radius, in.right + g.side, in.bottom + g.bottom};
}

bool FrameRainArea(const FrameGeometry& g, const RECT& bar, int index, RECT& area, RainFlow& flow) {
    if (!g.wide) return false;
    const LONG width = bar.right - bar.left;
    const LONG height = bar.bottom - bar.top;
    switch (index) {
        case 0: area = {0, 0, width, g.top - g.strip}; flow = RainFlow::Down; break;
        case 1: area = {0, 0, width, height}; flow = RainFlow::Right; break;
        case 2: area = {0, 0, width, height}; flow = RainFlow::Left; break;
        default: area = {0, g.radius, width, height}; flow = RainFlow::Up; break;
    }
    return area.right > area.left && area.bottom > area.top;
}

void PaintFrameBar(Canvas& canvas, const FrameGeometry& g, const RECT& bar, bool topBar, const TopBarContent& content,
                   FrameButton hover) {
    canvas.Clear();
    uint32_t* pixels = canvas.Pixels();
    const float monitorWidth = float(std::max(1L, g.monitor.right - g.monitor.left));
    const float s = std::max(1.0f, g.scale);
    const float stripTop = float(g.inner.top - g.strip);
    for (int y = 0; y < canvas.Height(); ++y) {
        const float py = bar.top + y + 0.5f;
        const bool inStrip = topBar && py >= stripTop && py < g.inner.top;
        for (int x = 0; x < canvas.Width(); ++x) {
            const float px = bar.left + x + 0.5f;
            // Distance outside the rounded opening: negative over the desktop, positive in the frame.
            const float d = SdRoundRect(px, py, float(g.inner.left), float(g.inner.top), float(g.inner.right),
                                        float(g.inner.bottom), float(g.radius));
            if (d <= -1.0f) continue;
            const float coverage = Clamp01(d + 0.5f);
            // The rim runs from Venom crimson to sheen blue across the monitor.
            const float t = Clamp01((px - g.monitor.left) / monitorWidth);
            const float rimR = venom::kFang.r + (venom::kSheen.r - venom::kFang.r) * t;
            const float rimG = venom::kFang.g + (venom::kSheen.g - venom::kFang.g) * t;
            const float rimB = venom::kFang.b + (venom::kSheen.b - venom::kFang.b) * t;
            const float line = Clamp01(1.0f - fabsf(d - 1.0f) / (1.1f * s));
            const float glow = expf(-std::max(d - 1.0f, 0.0f) / ((g.wide ? 6.0f : 3.5f) * s)) * 0.30f;
            // Glass: the strip is a shade lighter so its text reads over the rain.
            const Rgba base = inStrip ? venom::kHide : venom::kVoid;
            float r = base.r + rimR * glow;
            float gr = base.g + rimG * glow;
            float b = base.b + rimB * glow;
            r += (rimR - r) * line * 0.9f;
            gr += (rimG - gr) * line * 0.9f;
            b += (rimB - b) * line * 0.9f;
            pixels[size_t(y) * canvas.Width() + x] = PackPremultiplied(coverage * (inStrip ? 0.97f : 0.95f), r, gr, b);
        }
    }
    if (!topBar) return;

    const TopBarLayout t = ComputeTopBar(g, canvas.Width());
    const float sc = g.scale;
    if (g.wide) {
        // A hairline between the rain and the strip.
        canvas.FillRoundRect(float(g.side), stripTop - bar.top, float(canvas.Width() - g.side),
                             stripTop - bar.top + std::max(1.0f, sc), 0.0f, venom::WithAlpha(venom::kCardHi, 1.0f));
    }
    if (hover == FrameButton::Void) {
        const RECT& v = t.voidButton;
        canvas.FillRoundRect(float(v.left), float(v.top), float(v.right), float(v.bottom), 11.0f * sc,
                             venom::WithAlpha(venom::kFang, 0.14f));
    }
    venom::DrawMark(canvas, t.markX, t.centerY, t.markSize, venom::MarkStyle::Glow, hover == FrameButton::Void);
    const int textSize = int(lroundf(13.0f * sc));
    float x = t.textX;
    x += canvas.DrawString(content.brand, x, t.centerY, textSize, FW_BOLD, venom::kBone, venom::DisplayFont());
    x += 9.0f * sc;
    canvas.FillCircle(x, t.centerY, 1.6f * sc, venom::kFang);
    x += 9.0f * sc;
    canvas.DrawString(content.desktop, x, t.centerY, textSize, FW_SEMIBOLD, venom::kBoneDim, venom::UiFont());

    const Rgba idle = venom::WithAlpha(venom::kBoneDim, 0.85f);
    if (hover == FrameButton::Settings) {
        const RECT& b = t.settingsButton;
        canvas.FillRoundRect(float(b.left), float(b.top), float(b.right), float(b.bottom), 6.0f * sc,
                             venom::WithAlpha(venom::kFang, 0.14f));
    }
    DrawGear(canvas, t.gearX, t.centerY, sc, hover == FrameButton::Settings ? venom::kBone : idle);

    // The + for a new folder or app bubble; bubbles need the wide margin.
    if (g.wide) {
        if (hover == FrameButton::Add) {
            const RECT& a = t.addButton;
            canvas.FillRoundRect(float(a.left), float(a.top), float(a.right), float(a.bottom), 6.0f * sc,
                                 venom::WithAlpha(venom::kFang, 0.14f));
        }
        const Rgba plus = hover == FrameButton::Add ? venom::kBone : idle;
        const float arm = 5.5f * sc;
        canvas.DrawLine(t.addX - arm, t.centerY, t.addX + arm, t.centerY, 1.7f * sc, plus);
        canvas.DrawLine(t.addX, t.centerY - arm, t.addX, t.centerY + arm, 1.7f * sc, plus);
    }

    if (hover == FrameButton::Layout) {
        const RECT& l = t.layoutButton;
        canvas.FillRoundRect(float(l.left), float(l.top), float(l.right), float(l.bottom), 6.0f * sc,
                             venom::WithAlpha(venom::kFang, 0.14f));
    }
    // A tiny picture of the current zone layout.
    const float gw = t.glyphRight - t.glyphLeft;
    const float gh = t.glyphBottom - t.glyphTop;
    const Rgba glyphColor = hover == FrameButton::Layout ? venom::kBone : idle;
    if (content.layout) {
        for (const auto& f : content.layout->zones) {
            canvas.FillRoundRect(t.glyphLeft + f[0] * gw + 1.0f, t.glyphTop + f[1] * gh + 1.0f,
                                 t.glyphLeft + f[2] * gw - 1.0f, t.glyphTop + f[3] * gh - 1.0f, 1.5f * sc, glyphColor);
        }
    }
}

FrameButton FrameButtonAt(const FrameGeometry& geometry, int barWidth, POINT local) {
    const TopBarLayout t = ComputeTopBar(geometry, barWidth);
    if (PtInRect(&t.voidButton, local)) return FrameButton::Void;
    if (PtInRect(&t.settingsButton, local)) return FrameButton::Settings;
    if (PtInRect(&t.layoutButton, local)) return FrameButton::Layout;
    if (geometry.wide && PtInRect(&t.addButton, local)) return FrameButton::Add;
    return FrameButton::None;
}

bool ShellFrame::Init(HINSTANCE instance, HWND voidOverlay, Callbacks callbacks) {
    g_frame = this;
    instance_ = instance;
    overlay_ = voidOverlay;
    callbacks_ = std::move(callbacks);
    WNDCLASSEXW windowClass = {sizeof(windowClass)};
    windowClass.lpfnWndProc = BarProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = kBarClass;
    if (!RegisterClassExW(&windowClass)) return false;
    // A dragged bubble and a bubble's name float above everything and let the pointer through.
    const DWORD floating = WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE;
    ghost_ = CreateWindowExW(floating, kBarClass, L"VenomMind bubble", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, instance,
                             nullptr);
    label_ = CreateWindowExW(floating, kBarClass, L"VenomMind bubble name", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr,
                             instance, nullptr);
    return true;
}

void ShellFrame::SetEnabled(bool enabled) {
    if (enabled == enabled_) return;
    enabled_ = enabled;
    if (enabled) {
        Build();
    } else {
        Destroy();
        UpdateAnimationTimer();
        if (callbacks_.workAreaChanged) callbacks_.workAreaChanged();
        NotifyBarsChanged();
    }
}

void ShellFrame::SetStyle(const FrameStyle& style) {
    const bool resize = style.wide != style_.wide || (style.wide && style.margin != style_.margin) ||
                        style.dockBand != style_.dockBand;
    style_ = style;
    if (!enabled_) return;
    if (resize) {
        Build();  // new band sizes: reserve the space again
        if (callbacks_.workAreaChanged) callbacks_.workAreaChanged();
    } else {
        ComposeAll();
        UpdateAnimationTimer();
    }
}

void ShellFrame::SetAllMonitors(bool all) {
    if (all == allMonitors_) return;
    allMonitors_ = all;
    if (enabled_) {
        Build();
        if (callbacks_.workAreaChanged) callbacks_.workAreaChanged();
    }
}

void ShellFrame::SetSuppressed(HMONITOR monitor) {
    if (monitor == suppressed_) return;
    suppressed_ = monitor;
    for (auto& frame : monitors_) ShowBars(*frame, ShouldShow(*frame));
    UpdateAnimationTimer();
    NotifyBarsChanged();
}

void ShellFrame::SetAnimationAllowed(bool allowed) {
    if (allowed == animationAllowed_) return;
    animationAllowed_ = allowed;
    UpdateAnimationTimer();
}

void ShellFrame::SetBrand(const std::wstring& brand) {
    if (brand == brand_) return;
    brand_ = brand;
    for (auto& frame : monitors_) Paint(*frame, 0);
}

void ShellFrame::SetDesktopName(const std::wstring& name) {
    if (name == desktopName_) return;
    desktopName_ = name;
    for (auto& frame : monitors_) Paint(*frame, 0);
}

void ShellFrame::SetLayout(int index) {
    const int layout = std::clamp(index, 0, int(ZoneLayouts().size()) - 1);
    if (layout == layout_) return;
    layout_ = layout;
    for (auto& frame : monitors_) Paint(*frame, 0);
}

TopBarContent ShellFrame::Content() const { return {brand_, desktopName_, &ZoneLayouts()[layout_]}; }

void ShellFrame::SetStatsEnabled(bool enabled) {
    if (enabled == statsEnabled_) return;
    statsEnabled_ = enabled;
    for (auto& frame : monitors_) {
        PaintOrbsOverlay(*frame);
        Compose(*frame, 0);
    }
    LayoutBubbles();  // the orbs' room opens up or closes
}

void ShellFrame::SetStats(const SystemStats& stats) {
    stats_ = stats;
    if (!statsEnabled_) return;
    for (auto& frame : monitors_) {
        if (!ShouldShow(*frame)) continue;
        PaintOrbsOverlay(*frame);
        Compose(*frame, 0);
    }
}

void ShellFrame::PaintOrbsOverlay(MonitorFrame& frame) {
    const RECT& bar = frame.bars[0].rect;
    const OrbsLayout layout = LayoutOrbs(frame.geometry, bar.right - bar.left);
    frame.orbsBox = statsEnabled_ ? layout.box : RECT{};
    const int width = frame.orbsBox.right - frame.orbsBox.left;
    const int height = frame.orbsBox.bottom - frame.orbsBox.top;
    if (width <= 0 || height <= 0 || !frame.orbs.Resize(width, height)) {
        frame.orbs.Resize(0, 0);
        return;
    }
    PaintOrbs(frame.orbs, layout, stats_, frame.geometry.scale, frame.hover == FrameButton::Stats);
}

// ---- Bubbles ----

ShellFrame::MonitorFrame* ShellFrame::FrameFor(const Shortcut& shortcut) {
    for (auto& frame : monitors_)
        if (!shortcut.monitor.empty() && frame->device == shortcut.monitor) return frame.get();
    // Its monitor is gone (or has no frame): show it on the main one.
    for (auto& frame : monitors_)
        if (frame->primary) return frame.get();
    return monitors_.empty() ? nullptr : monitors_.front().get();
}

std::vector<RECT> ShellFrame::Reserved(const MonitorFrame& frame, int bar) const {
    std::vector<RECT> reserved;
    const RECT& rect = frame.bars[bar].rect;
    if (bar == 0 && statsEnabled_) {
        const OrbsLayout orbs = LayoutOrbs(frame.geometry, rect.right - rect.left);
        if (!IsRectEmpty(&orbs.box)) reserved.push_back(orbs.box);
    }
    RECT overlap;
    if (haveDockArea_ && IntersectRect(&overlap, &dockArea_, &rect)) {
        OffsetRect(&overlap, -rect.left, -rect.top);
        reserved.push_back(overlap);
    }
    return reserved;
}

const IconImage* ShellFrame::BubbleIcon(const Shortcut& shortcut, int size) {
    const std::wstring key = shortcut.target + L"|" + std::to_wstring(size);
    auto found = bubbleIcons_.find(key);
    if (found == bubbleIcons_.end()) {
        IconImage icon;
        if (!ShellIcon(shortcut.target, size, icon)) icon = IconImage();  // remembered as none
        found = bubbleIcons_.emplace(key, std::move(icon)).first;
    }
    return found->second.Empty() ? nullptr : &found->second;
}

void ShellFrame::PaintBubbleSprite(MonitorFrame& frame, Bubble& bubble) {
    const BubbleMetrics m = MeasureBubbles(frame.geometry);
    if (bubble.index < 0 || bubble.index >= int(shortcuts_.size()) || !bubble.sprite.Resize(m.cellWidth, m.cellHeight))
        return;
    const Shortcut& shortcut = shortcuts_[bubble.index];
    const BubbleState state = bubble.index < int(states_.size()) ? states_[bubble.index] : BubbleState::Closed;
    PaintBubble(bubble.sprite, m, BubbleIcon(shortcut, int(lroundf(m.diameter * 0.56f))), shortcut.name, shortcut.kind,
                bubble.index == hoverShortcut_ && !dragging_, frame.geometry.scale, state);
}

void ShellFrame::LayoutBubbles() {
    for (auto& frame : monitors_) {
        for (auto& list : frame->bubbles) list.clear();
        for (auto& sockets : frame->sockets) sockets.clear();
    }
    for (auto& owned : monitors_) {
        MonitorFrame& frame = *owned;
        const BubbleMetrics m = MeasureBubbles(frame.geometry);
        if (m.usable) {
            // Evenly spaced sockets in every band, and their three looks for each kind.
            RECT areas[4] = {};
            for (int bar = 0; bar < 4; ++bar) {
                if (!frame.bars[bar].window) continue;
                areas[bar] = BubbleArea(frame.geometry, frame.bars[bar].rect, bar);
                frame.sockets[bar] = BandSockets(areas[bar], m, Reserved(frame, bar));
            }
            for (int kind = 0; kind < 2; ++kind) {
                for (int look = 0; look < 3; ++look) {
                    Canvas& sprite = frame.socketSprites[kind][look];
                    if (sprite.Resize(m.cellWidth, m.cellHeight))
                        PaintSocket(sprite, m, kind == 0 ? ShortcutKind::App : ShortcutKind::Folder, SocketLook(look),
                                    frame.geometry.scale);
                }
            }
            const auto place = [&](int bar, int index, int socket) {
                auto bubble = std::make_unique<Bubble>();
                bubble->index = index;
                bubble->socket = socket;
                bubble->cell = frame.sockets[bar][socket];
                PaintBubbleSprite(frame, *bubble);
                frame.bubbles[bar].push_back(std::move(bubble));
            };
            // Each kind's pair of bands. A bubble takes the free socket nearest where it was put; if its band is
            // full, it goes to the other band of the pair.
            for (const auto& pair : {std::pair<int, int>{1, 2}, std::pair<int, int>{0, 3}}) {
                std::vector<bool> taken[4];
                std::vector<std::pair<int, int>> spill;  // (shortcut, the band it spills into)
                for (const int bar : {pair.first, pair.second}) {
                    std::vector<int> indices;
                    std::vector<RECT> wanted;
                    for (size_t i = 0; i < shortcuts_.size(); ++i) {
                        const Shortcut& shortcut = shortcuts_[i];
                        if (shortcut.band != bar || FrameFor(shortcut) != &frame) continue;
                        indices.push_back(int(i));
                        wanted.push_back(CellAt(areas[bar], m, shortcut.fx, shortcut.fy));
                    }
                    const std::vector<int> sockets = AssignSockets(frame.sockets[bar], wanted, taken[bar]);
                    for (size_t k = 0; k < sockets.size(); ++k) {
                        if (sockets[k] >= 0) {
                            place(bar, indices[k], sockets[k]);
                        } else {
                            spill.push_back({indices[k], bar == pair.first ? pair.second : pair.first});
                        }
                    }
                }
                for (const auto& [index, bar] : spill) {
                    const Shortcut& shortcut = shortcuts_[index];
                    const std::vector<int> socket =
                        AssignSockets(frame.sockets[bar], {CellAt(areas[bar], m, shortcut.fx, shortcut.fy)}, taken[bar]);
                    if (!socket.empty() && socket[0] >= 0) place(bar, index, socket[0]);
                }
            }
        }
        for (int bar = 0; bar < 4; ++bar) Compose(frame, bar);
    }
}

void ShellFrame::ComposeEverything() {
    for (auto& frame : monitors_)
        for (int bar = 0; bar < 4; ++bar) Compose(*frame, bar);
}

ShellFrame::Bubble* ShellFrame::BubbleAt(MonitorFrame& frame, int bar, POINT local) {
    const BubbleMetrics m = MeasureBubbles(frame.geometry);
    for (auto& bubble : frame.bubbles[bar]) {
        if (dragging_ && bubble->index == pressed_) continue;
        if (!PtInRect(&bubble->cell, local)) continue;
        if (BubbleHit(m, {local.x - bubble->cell.left, local.y - bubble->cell.top})) return bubble.get();
    }
    return nullptr;
}

bool ShellFrame::FindBubble(int index, MonitorFrame** frame, int* bar, Bubble** bubble) {
    for (auto& owned : monitors_) {
        for (int b = 0; b < 4; ++b) {
            for (auto& candidate : owned->bubbles[b]) {
                if (candidate->index != index) continue;
                *frame = owned.get();
                *bar = b;
                *bubble = candidate.get();
                return true;
            }
        }
    }
    return false;
}

void ShellFrame::SetBubbleHover(int index) {
    if (index == hoverShortcut_) return;
    const int previous = hoverShortcut_;
    hoverShortcut_ = index;
    for (const int changed : {previous, index}) {
        MonitorFrame* frame = nullptr;
        int bar = -1;
        Bubble* bubble = nullptr;
        if (changed < 0 || !FindBubble(changed, &frame, &bar, &bubble)) continue;
        PaintBubbleSprite(*frame, *bubble);
        Compose(*frame, bar);
    }
    ShowBubbleLabel(index);
    if (callbacks_.hoverShortcut) callbacks_.hoverShortcut(index);
}

void ShellFrame::ShowBubbleLabel(int index) {
    MonitorFrame* frame = nullptr;
    int bar = -1;
    Bubble* bubble = nullptr;
    // Names show under the bubbles when the margin has room; otherwise as a label beside the one under the pointer.
    if (index < 0 || dragging_ || !FindBubble(index, &frame, &bar, &bubble) || MeasureBubbles(frame->geometry).labels) {
        if (label_) ShowWindow(label_, SW_HIDE);
        return;
    }
    const float s = frame->geometry.scale;
    const std::wstring& name = shortcuts_[index].name;
    const SIZE size = BubbleLabelSize(name, s);
    if (!labelCanvas_.Resize(size.cx, size.cy)) return;
    PaintBubbleLabel(labelCanvas_, name, s);
    RECT cell = bubble->cell;
    const RECT& rect = frame->bars[bar].rect;
    OffsetRect(&cell, rect.left, rect.top);
    const LONG gap = LONG(lroundf(6.0f * s));
    POINT at = {cell.right + gap, (cell.top + cell.bottom) / 2 - size.cy / 2};
    if (bar == 2) at.x = cell.left - gap - size.cx;  // the right band: toward the desktop
    labelCanvas_.Present(label_, at);
    SetWindowPos(label_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

bool ShellFrame::DropTarget(POINT centre, MonitorFrame** target, int* targetBar, int* targetSocket) {
    // The nearest socket in a band its kind may live in, on any monitor with the frame.
    long long best = LLONG_MAX;
    for (auto& frame : monitors_) {
        if (!ShouldShow(*frame)) continue;
        for (int bar = 0; bar < 4; ++bar) {
            if (!BandAllows(dragKind_, bar)) continue;
            const RECT& rect = frame->bars[bar].rect;
            for (size_t k = 0; k < frame->sockets[bar].size(); ++k) {
                const RECT& socket = frame->sockets[bar][k];
                const long long dx = rect.left + (socket.left + socket.right) / 2 - centre.x;
                const long long dy = rect.top + (socket.top + socket.bottom) / 2 - centre.y;
                if (dx * dx + dy * dy >= best) continue;
                best = dx * dx + dy * dy;
                *target = frame.get();
                *targetBar = bar;
                *targetSocket = int(k);
            }
        }
    }
    return best != LLONG_MAX;
}

void ShellFrame::StartDrag() {
    MonitorFrame* frame = nullptr;
    int bar = -1;
    Bubble* bubble = nullptr;
    if (!FindBubble(pressed_, &frame, &bar, &bubble)) {
        pressed_ = -1;
        return;
    }
    const BubbleMetrics m = MeasureBubbles(frame->geometry);
    const Shortcut& shortcut = shortcuts_[pressed_];
    if (!ghostCanvas_.Resize(m.cellWidth, m.cellHeight)) return;
    const BubbleState state = pressed_ < int(states_.size()) ? states_[pressed_] : BubbleState::Closed;
    PaintBubble(ghostCanvas_, m, BubbleIcon(shortcut, int(lroundf(m.diameter * 0.56f))), shortcut.name, shortcut.kind,
                true, frame->geometry.scale, state);
    // Remembered so a bubble it's dropped on can take its old socket.
    dragKind_ = shortcut.kind;
    dragFromDevice_ = frame->device;
    dragFromBar_ = bar;
    FractionsOf(BubbleArea(frame->geometry, frame->bars[bar].rect, bar), m,
                {(bubble->cell.left + bubble->cell.right) / 2, (bubble->cell.top + bubble->cell.bottom) / 2}, dragFromFx_,
                dragFromFy_);
    dropFrame_ = frame;
    dropBar_ = bar;
    dropSocket_ = bubble->socket;
    dragging_ = true;
    if (label_) ShowWindow(label_, SW_HIDE);
    ComposeEverything();  // every socket it could go to opens up, and its own empties
}

void ShellFrame::MoveDrag(POINT screen) {
    ghostCanvas_.Present(ghost_, {screen.x - grab_.x, screen.y - grab_.y}, 235);
    if (!IsWindowVisible(ghost_))
        SetWindowPos(ghost_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    // Light the socket it would land in.
    const POINT centre = {screen.x - grab_.x + ghostCanvas_.Width() / 2, screen.y - grab_.y + ghostCanvas_.Height() / 2};
    MonitorFrame* frame = nullptr;
    int bar = -1, socket = -1;
    if (!DropTarget(centre, &frame, &bar, &socket)) frame = nullptr;
    if (frame == dropFrame_ && bar == dropBar_ && socket == dropSocket_) return;
    MonitorFrame* oldFrame = dropFrame_;
    const int oldBar = dropBar_;
    dropFrame_ = frame;
    dropBar_ = bar;
    dropSocket_ = socket;
    if (oldFrame && oldBar >= 0) Compose(*oldFrame, oldBar);
    if (frame && bar >= 0 && (frame != oldFrame || bar != oldBar)) Compose(*frame, bar);
}

void ShellFrame::Drop(POINT screen) {
    if (ghost_) ShowWindow(ghost_, SW_HIDE);
    const int index = pressed_;
    dragging_ = false;
    pressed_ = -1;
    dropFrame_ = nullptr;
    dropBar_ = dropSocket_ = -1;
    const POINT centre = {screen.x - grab_.x + ghostCanvas_.Width() / 2, screen.y - grab_.y + ghostCanvas_.Height() / 2};
    MonitorFrame* frame = nullptr;
    int bar = -1, socket = -1;
    const bool found = index >= 0 && index < int(shortcuts_.size()) && DropTarget(centre, &frame, &bar, &socket);
    if (found) {
        // A bubble already in that socket swaps places with the one dropped on it.
        for (const auto& other : frame->bubbles[bar]) {
            if (other->socket != socket || other->index == index) continue;
            Shortcut& swapped = shortcuts_[other->index];
            swapped.monitor = dragFromDevice_;
            swapped.band = dragFromBar_;
            swapped.fx = dragFromFx_;
            swapped.fy = dragFromFy_;
        }
        Shortcut& moved = shortcuts_[index];
        const RECT& cell = frame->sockets[bar][socket];
        FractionsOf(BubbleArea(frame->geometry, frame->bars[bar].rect, bar), MeasureBubbles(frame->geometry),
                    {(cell.left + cell.right) / 2, (cell.top + cell.bottom) / 2}, moved.fx, moved.fy);
        moved.band = bar;
        moved.monitor = frame->device;
    }
    hoverShortcut_ = -1;
    LayoutBubbles();
    if (found) NotifyShortcuts();
}

void ShellFrame::CancelDrag() {
    const bool wasDragging = dragging_;
    if (ghost_) ShowWindow(ghost_, SW_HIDE);
    dragging_ = false;
    pressed_ = -1;
    dropFrame_ = nullptr;
    dropBar_ = dropSocket_ = -1;
    if (wasDragging) LayoutBubbles();
}

void ShellFrame::NotifyShortcuts() {
    if (callbacks_.shortcutsChanged) callbacks_.shortcutsChanged(shortcuts_);
}

void ShellFrame::SetShortcuts(const std::vector<Shortcut>& shortcuts) {
    CancelDrag();
    shortcuts_ = shortcuts;
    hoverShortcut_ = -1;
    if (label_) ShowWindow(label_, SW_HIDE);
    LayoutBubbles();
}

bool ShellFrame::AddShortcut(Shortcut shortcut, POINT at) {
    for (const Shortcut& other : shortcuts_)
        if (_wcsicmp(other.target.c_str(), shortcut.target.c_str()) == 0) return false;
    MonitorFrame* frame = nullptr;
    const HMONITOR monitor = MonitorFromPoint(at, MONITOR_DEFAULTTOPRIMARY);
    for (auto& candidate : monitors_)
        if (candidate->monitor == monitor) frame = candidate.get();
    if (!frame) frame = FrameFor(Shortcut());
    // Apps take the first free socket down the left band, then the right; folders along the top, then the bottom.
    const int bands[2] = {shortcut.kind == ShortcutKind::App ? 1 : 0, shortcut.kind == ShortcutKind::App ? 2 : 3};
    shortcut.band = bands[0];
    shortcut.fx = shortcut.fy = 0.0f;
    if (frame) {
        shortcut.monitor = frame->device;
        const BubbleMetrics m = MeasureBubbles(frame->geometry);
        bool placed = false;
        for (const int bar : bands) {
            const std::vector<RECT>& sockets = frame->sockets[bar];
            if (placed || !m.usable || sockets.empty()) continue;
            std::vector<bool> occupied(sockets.size(), false);
            for (const auto& bubble : frame->bubbles[bar])
                if (bubble->socket >= 0 && bubble->socket < int(sockets.size())) occupied[bubble->socket] = true;
            for (size_t k = 0; k < sockets.size() && !placed; ++k) {
                if (occupied[k]) continue;
                shortcut.band = bar;
                FractionsOf(BubbleArea(frame->geometry, frame->bars[bar].rect, bar), m,
                            {(sockets[k].left + sockets[k].right) / 2, (sockets[k].top + sockets[k].bottom) / 2},
                            shortcut.fx, shortcut.fy);
                placed = true;
            }
        }
    }
    shortcuts_.push_back(shortcut);
    LayoutBubbles();
    NotifyShortcuts();
    return true;
}

void ShellFrame::RemoveShortcut(int index) {
    if (index < 0 || index >= int(shortcuts_.size())) return;
    CancelDrag();
    shortcuts_.erase(shortcuts_.begin() + index);
    if (index < int(states_.size())) states_.erase(states_.begin() + index);
    hoverShortcut_ = -1;
    if (label_) ShowWindow(label_, SW_HIDE);
    LayoutBubbles();
    NotifyShortcuts();
}

void ShellFrame::SetShortcutStates(const std::vector<BubbleState>& states) {
    if (states == states_) return;
    states_ = states;
    for (auto& frame : monitors_) {
        for (int bar = 0; bar < 4; ++bar) {
            if (frame->bubbles[bar].empty()) continue;
            for (auto& bubble : frame->bubbles[bar]) PaintBubbleSprite(*frame, *bubble);
            Compose(*frame, bar);
        }
    }
}

void ShellFrame::SetShowSockets(bool show) {
    if (show == showSockets_) return;
    showSockets_ = show;
    ComposeEverything();
}

bool ShellFrame::ShortcutAnchor(int index, RECT& round, int& band, RECT& monitor, float& scale) {
    MonitorFrame* frame = nullptr;
    int bar = -1;
    Bubble* bubble = nullptr;
    if (!FindBubble(index, &frame, &bar, &bubble)) return false;
    const BubbleMetrics m = MeasureBubbles(frame->geometry);
    const RECT& rect = frame->bars[bar].rect;
    const LONG left = rect.left + bubble->cell.left + (m.cellWidth - m.diameter) / 2;
    const LONG top = rect.top + bubble->cell.top;
    round = {left, top, left + m.diameter, top + m.diameter};
    band = bar;
    monitor = frame->geometry.monitor;
    scale = frame->geometry.scale;
    return true;
}

void ShellFrame::SetDockArea(const RECT* area) {
    const bool have = area != nullptr;
    if (have == haveDockArea_ && (!have || EqualRect(area, &dockArea_))) return;
    haveDockArea_ = have;
    dockArea_ = have ? *area : RECT{};
    LayoutBubbles();
}

void ShellFrame::NotifyBarsChanged() {
    if (callbacks_.barsChanged) callbacks_.barsChanged();
}

bool ShellFrame::DockSlot(RECT& slot, float& scale) const {
    for (const auto& frame : monitors_) {
        if (!frame->primary || !ShouldShow(*frame) || !frame->bars[3].window) continue;
        const FrameGeometry& g = frame->geometry;
        const RECT& band = frame->bars[3].reserved;
        slot = {g.inner.left - g.side, band.top, g.inner.right + g.side, band.bottom};
        scale = g.scale;
        return slot.bottom > slot.top && slot.right > slot.left;
    }
    return false;
}

void ShellFrame::ScheduleRebuild() {
    if (!enabled_) return;
    rebuildTimer_ = SetTimer(nullptr, rebuildTimer_, 400, OnTimer);
}

void ShellFrame::ScheduleLayout() { layoutTimer_ = SetTimer(nullptr, layoutTimer_, 150, OnTimer); }

void CALLBACK ShellFrame::OnTimer(HWND, UINT, UINT_PTR id, DWORD) {
    ShellFrame* self = g_frame;
    if (!self) {
        KillTimer(nullptr, id);
        return;
    }
    if (id == self->animationTimer_) {
        self->Animate();  // repeating
        return;
    }
    KillTimer(nullptr, id);
    if (id == self->rebuildTimer_) {
        self->rebuildTimer_ = kNoTimer;
        self->Build();
    } else if (id == self->layoutTimer_) {
        self->layoutTimer_ = kNoTimer;
        self->LayoutAll();
    }
}

void ShellFrame::Shutdown() {
    for (UINT_PTR* timer : {&rebuildTimer_, &layoutTimer_, &animationTimer_}) {
        if (*timer) KillTimer(nullptr, *timer);
        *timer = kNoTimer;
    }
    Destroy();
    enabled_ = false;
    for (HWND* window : {&ghost_, &label_}) {
        if (*window) DestroyWindow(*window);
        *window = nullptr;
    }
}

void ShellFrame::RemoveAppBars() {
    for (auto& frame : monitors_) {
        for (Bar& bar : frame->bars) {
            if (!bar.window) continue;
            APPBARDATA data = {sizeof(data)};
            data.hWnd = bar.window;
            SHAppBarMessage(ABM_REMOVE, &data);
        }
    }
}

void ShellFrame::Destroy() {
    CancelDrag();
    hoverShortcut_ = -1;
    if (label_) ShowWindow(label_, SW_HIDE);
    RemoveAppBars();
    for (auto& frame : monitors_)
        for (Bar& bar : frame->bars)
            if (bar.window) DestroyWindow(bar.window);
    monitors_.clear();
}

BOOL CALLBACK ShellFrame::OnMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM self) {
    reinterpret_cast<ShellFrame*>(self)->AddMonitor(monitor);
    return TRUE;
}

void ShellFrame::Build() {
    Destroy();
    if (!enabled_) return;
    EnumDisplayMonitors(nullptr, nullptr, OnMonitor, reinterpret_cast<LPARAM>(this));
    LayoutAll();
    UpdateAnimationTimer();
    Log("Shell frame (%s) on %zu monitor(s)", style_.wide ? "wide" : "slim", monitors_.size());
}

void ShellFrame::AddMonitor(HMONITOR monitor) {
    MONITORINFOEXW info = {};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info)) return;
    if (!allMonitors_ && !(info.dwFlags & MONITORINFOF_PRIMARY)) return;
    auto frame = std::make_unique<MonitorFrame>();
    frame->monitor = monitor;
    frame->device = info.szDevice;
    frame->primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
    frame->geometry = FrameSizes(info.rcMonitor, MonitorScale(monitor), style_);
    // The dock lives in the main monitor's bottom band, so that band grows to hold it.
    if (frame->primary && style_.dockBand > 0)
        frame->geometry.bottom = std::max(frame->geometry.bottom, int(lroundf(style_.dockBand * frame->geometry.scale)));
    const float s = frame->geometry.scale;
    frame->glyphs.Build(int(lroundf(13.0f * s)), int(lroundf(16.0f * s)));
    const UINT edges[4] = {ABE_TOP, ABE_LEFT, ABE_RIGHT, ABE_BOTTOM};
    for (int i = 0; i < 4; ++i) {
        Bar& bar = frame->bars[i];
        bar.edge = edges[i];
        // Never activated, so clicking the bar doesn't take focus from your app.
        bar.window = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, kBarClass,
                                     L"Void Shell frame", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, instance_, nullptr);
        if (!bar.window) continue;
        APPBARDATA data = {sizeof(data)};
        data.hWnd = bar.window;
        data.uCallbackMessage = kAppBarMessage;
        if (!SHAppBarMessage(ABM_NEW, &data)) Log("Could not register a frame appbar");
    }
    monitors_.push_back(std::move(frame));
}

// Asks the shell for each band's space. Returns true if any reservation changed.
bool ShellFrame::Reserve(MonitorFrame& frame) {
    FrameGeometry& g = frame.geometry;
    const int thickness[4] = {g.top, g.side, g.side, g.bottom};
    bool changed = false;
    for (int i = 0; i < 4; ++i) {
        Bar& bar = frame.bars[i];
        if (!bar.window) continue;
        APPBARDATA data = {sizeof(data)};
        data.hWnd = bar.window;
        data.uEdge = bar.edge;
        data.rc = g.monitor;
        ApplyThickness(data.rc, bar.edge, thickness[i]);
        SHAppBarMessage(ABM_QUERYPOS, &data);  // the shell moves it clear of the taskbar and other appbars
        ApplyThickness(data.rc, bar.edge, thickness[i]);
        // Only re-reserve when something moved; every reservation notifies the other appbars.
        if (!EqualRect(&data.rc, &bar.reserved)) {
            SHAppBarMessage(ABM_SETPOS, &data);
            bar.reserved = data.rc;
            changed = true;
        }
    }
    g.inner = {frame.bars[1].reserved.right, frame.bars[0].reserved.bottom, frame.bars[2].reserved.left,
               frame.bars[3].reserved.top};
    RECT rects[4];
    FrameBarRects(g, rects);
    for (int i = 0; i < 4; ++i) {
        Bar& bar = frame.bars[i];
        if (EqualRect(&bar.rect, &rects[i]) && !bar.rain.Empty()) continue;
        bar.rect = rects[i];
        RECT area;
        RainFlow flow;
        if (FrameRainArea(g, bar.rect, i, area, flow)) {
            bar.rain.Reset(area, flow, frame.glyphs, 0x9E3779B9u * unsigned(i + 1) + unsigned(g.monitor.left));
        } else {
            bar.rain = RainField();
        }
    }
    return changed;
}

void ShellFrame::LayoutAll() {
    bool changed = false;
    for (auto& frame : monitors_) changed |= Reserve(*frame);
    for (auto& frame : monitors_) {
        for (int i = 0; i < 4; ++i) Paint(*frame, i);
        if (ShouldShow(*frame)) ShowBars(*frame, true);
    }
    LayoutBubbles();
    if (changed && callbacks_.workAreaChanged) callbacks_.workAreaChanged();
    NotifyBarsChanged();
}

void ShellFrame::Paint(MonitorFrame& frame, int index) {
    Bar& bar = frame.bars[index];
    const int width = bar.rect.right - bar.rect.left;
    const int height = bar.rect.bottom - bar.rect.top;
    if (!bar.window || width <= 0 || height <= 0 || !bar.still.Resize(width, height)) return;
    PaintFrameBar(bar.still, frame.geometry, bar.rect, index == 0, Content(),
                  index == 0 ? frame.hover : FrameButton::None);
    if (index == 0) PaintOrbsOverlay(frame);
    Compose(frame, index);
}

bool ShellFrame::Raining(const MonitorFrame& frame) const {
    return style_.wide && style_.rain && ShouldShow(frame);
}

void ShellFrame::Compose(MonitorFrame& frame, int index) {
    Bar& bar = frame.bars[index];
    if (!bar.window || !bar.canvas.Resize(bar.still.Width(), bar.still.Height())) return;
    bar.canvas.CopyFrom(bar.still);
    if (Raining(frame) && !bar.rain.Empty()) bar.rain.Draw(bar.canvas, frame.glyphs, style_.rainColor);
    // Empty sockets sit over the rain: always, if wanted, and while a bubble is dragged the ones it may go to open up.
    const std::vector<RECT>& sockets = frame.sockets[index];
    if (!sockets.empty() && (showSockets_ || dragging_)) {
        const int kind = index == 1 || index == 2 ? 0 : 1;
        const bool open = dragging_ && BandAllows(dragKind_, index);
        std::vector<bool> occupied(sockets.size(), false);
        for (const auto& bubble : frame.bubbles[index])
            if (!(dragging_ && bubble->index == pressed_) && bubble->socket >= 0 && bubble->socket < int(sockets.size()))
                occupied[bubble->socket] = true;
        for (size_t k = 0; k < sockets.size(); ++k) {
            if (occupied[k]) continue;
            SocketLook look = SocketLook::Idle;
            if (open) look = &frame == dropFrame_ && index == dropBar_ && int(k) == dropSocket_ ? SocketLook::Target : SocketLook::Open;
            if (look == SocketLook::Idle && !showSockets_) continue;
            bar.canvas.Draw(frame.socketSprites[kind][int(look)], sockets[k].left, sockets[k].top);
        }
    }
    // Bubbles and orbs float over the rain. A bubble being dragged is drawn by the ghost window instead.
    for (const auto& bubble : frame.bubbles[index])
        if (!(dragging_ && bubble->index == pressed_)) bar.canvas.Draw(bubble->sprite, bubble->cell.left, bubble->cell.top);
    if (index == 0 && statsEnabled_ && frame.orbs.Width() > 0)
        bar.canvas.Draw(frame.orbs, frame.orbsBox.left, frame.orbsBox.top);
    bar.canvas.Present(bar.window, {bar.rect.left, bar.rect.top});
}

void ShellFrame::ComposeAll() {
    for (auto& frame : monitors_)
        for (int i = 0; i < 4; ++i) Compose(*frame, i);
}

void ShellFrame::Animate() {
    LARGE_INTEGER now, frequency;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&frequency);
    const float dt = std::min(0.2f, float(double(now.QuadPart - lastFrame_.QuadPart) / double(frequency.QuadPart)));
    lastFrame_ = now;
    for (auto& frame : monitors_) {
        if (!Raining(*frame)) continue;
        for (int i = 0; i < 4; ++i) {
            frame->bars[i].rain.Step(dt);
            Compose(*frame, i);
        }
    }
}

// The animation timer only runs while some monitor actually shows rain; otherwise Void Shell stays asleep.
void ShellFrame::UpdateAnimationTimer() {
    bool wanted = enabled_ && animationAllowed_ && style_.wide && style_.rain;
    if (wanted) {
        wanted = false;
        for (const auto& frame : monitors_) wanted = wanted || Raining(*frame);
    }
    if (wanted && !animationTimer_) {
        QueryPerformanceCounter(&lastFrame_);
        animationTimer_ = SetTimer(nullptr, 0, kRainFrameMs, OnTimer);
    } else if (!wanted && animationTimer_) {
        KillTimer(nullptr, animationTimer_);
        animationTimer_ = kNoTimer;
    }
}

void ShellFrame::ShowBars(MonitorFrame& frame, bool show) {
    // While the void is open, stay just under it rather than jumping on top.
    const HWND after = IsWindowVisible(overlay_) ? overlay_ : HWND_TOPMOST;
    for (Bar& bar : frame.bars) {
        if (!bar.window) continue;
        if (show) {
            SetWindowPos(bar.window, after, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        } else {
            ShowWindow(bar.window, SW_HIDE);
        }
    }
}

void ShellFrame::OnFullscreen(MonitorFrame& frame, bool opened) {
    if (opened) {
        // The void is fullscreen too, but it covers the frame anyway and hiding it would flicker on the way out.
        if (IsWindowVisible(overlay_)) return;
        const HWND foreground = GetForegroundWindow();
        if (!foreground || MonitorFromWindow(foreground, MONITOR_DEFAULTTONULL) != frame.monitor) return;
        if (!frame.hidden) {
            frame.hidden = true;
            ShowBars(frame, false);
        }
    } else {
        for (auto& other : monitors_) {
            if (!other->hidden) continue;
            other->hidden = false;
            ShowBars(*other, ShouldShow(*other));
        }
    }
    UpdateAnimationTimer();
    NotifyBarsChanged();
}

ShellFrame::MonitorFrame* ShellFrame::FrameOf(HWND window, int* bar) {
    for (auto& frame : monitors_) {
        for (int i = 0; i < 4; ++i) {
            if (frame->bars[i].window == window) {
                *bar = i;
                return frame.get();
            }
        }
    }
    return nullptr;
}

LRESULT CALLBACK ShellFrame::BarProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    int bar = -1;
    MonitorFrame* frame = g_frame ? g_frame->FrameOf(window, &bar) : nullptr;
    if (frame) return g_frame->HandleBar(*frame, bar, window, message, wParam, lParam);
    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT ShellFrame::HandleBar(MonitorFrame& frame, int bar, HWND window, UINT message, WPARAM wParam,
                              LPARAM lParam) {
    switch (message) {
        case kAppBarMessage:
            if (wParam == ABN_POSCHANGED || wParam == ABN_STATECHANGE) ScheduleLayout();
            if (wParam == ABN_FULLSCREENAPP) OnFullscreen(frame, lParam != 0);
            return 0;
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_MOUSEMOVE: {
            const POINT local = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            const RECT& rect = frame.bars[bar].rect;
            const POINT screen = {rect.left + local.x, rect.top + local.y};
            if (pressed_ >= 0) {
                // A bubble follows the pointer once it has moved a little; a smaller wobble is still a click.
                if (!dragging_ && (abs(screen.x - pressAt_.x) > GetSystemMetrics(SM_CXDRAG) ||
                                   abs(screen.y - pressAt_.y) > GetSystemMetrics(SM_CYDRAG)))
                    StartDrag();
                if (dragging_) MoveDrag(screen);
                return 0;
            }
            const Bubble* bubble = BubbleAt(frame, bar, local);
            SetBubbleHover(bubble ? bubble->index : -1);
            if (bar == 0) {
                FrameButton hover = bubble ? FrameButton::None : FrameButtonAt(frame.geometry, rect.right - rect.left, local);
                if (hover == FrameButton::None && !bubble && statsEnabled_ && PtInRect(&frame.orbsBox, local))
                    hover = FrameButton::Stats;
                if (hover != frame.hover) {
                    frame.hover = hover;
                    Paint(frame, 0);
                }
            }
            TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, window, 0};
            TrackMouseEvent(&track);
            return 0;
        }
        case WM_LBUTTONDOWN: {
            const POINT local = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (Bubble* bubble = BubbleAt(frame, bar, local)) {
                const RECT& rect = frame.bars[bar].rect;
                if (callbacks_.bubblePressed) callbacks_.bubblePressed();
                pressed_ = bubble->index;
                pressAt_ = {rect.left + local.x, rect.top + local.y};
                grab_ = {local.x - bubble->cell.left, local.y - bubble->cell.top};
                SetCapture(window);  // keep the pointer while it's dragged across bars and monitors
            }
            return 0;
        }
        case WM_RBUTTONUP: {
            const POINT local = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            const Bubble* bubble = pressed_ < 0 ? BubbleAt(frame, bar, local) : nullptr;
            if (bubble && callbacks_.shortcutMenu) {
                const RECT& rect = frame.bars[bar].rect;
                callbacks_.shortcutMenu(bubble->index, {rect.left + local.x, rect.top + local.y});
            }
            return 0;
        }
        case WM_MBUTTONUP: {
            // Middle click starts another one, as on the taskbar.
            const Bubble* bubble = pressed_ < 0 ? BubbleAt(frame, bar, {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}) : nullptr;
            if (bubble && callbacks_.launchShortcut) callbacks_.launchShortcut(bubble->index);
            return 0;
        }
        case WM_CAPTURECHANGED:
            if (pressed_ >= 0 && reinterpret_cast<HWND>(lParam) != window) CancelDrag();
            return 0;
        case WM_MOUSELEAVE:
            if (pressed_ < 0) SetBubbleHover(-1);
            if (bar == 0 && frame.hover != FrameButton::None) {
                frame.hover = FrameButton::None;
                Paint(frame, 0);
            }
            return 0;
        case WM_LBUTTONUP:
            if (pressed_ >= 0) {
                const RECT& from = frame.bars[bar].rect;
                const POINT screen = {from.left + GET_X_LPARAM(lParam), from.top + GET_Y_LPARAM(lParam)};
                const int index = pressed_;
                const bool dragged = dragging_;
                if (dragged) {
                    Drop(screen);
                } else {
                    pressed_ = -1;
                }
                if (GetCapture() == window) ReleaseCapture();
                if (!dragged && callbacks_.openShortcut) callbacks_.openShortcut(index);
                return 0;
            }
            if (bar == 0) {
                const RECT& rect = frame.bars[0].rect;
                const POINT local = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                FrameButton button = FrameButtonAt(frame.geometry, rect.right - rect.left, local);
                if (button == FrameButton::Add && callbacks_.addShortcut) {
                    const TopBarLayout t = ComputeTopBar(frame.geometry, rect.right - rect.left);
                    callbacks_.addShortcut({rect.left + (t.addButton.left + t.addButton.right) / 2,
                                            rect.top + frame.geometry.top});
                }
                if (button == FrameButton::None && statsEnabled_ && PtInRect(&frame.orbsBox, local)) button = FrameButton::Stats;
                if (button == FrameButton::Stats && callbacks_.openStats) callbacks_.openStats();
                if (button == FrameButton::Void && callbacks_.openVoid) callbacks_.openVoid();
                if (button == FrameButton::Settings && callbacks_.openSettings) callbacks_.openSettings();
                if (button == FrameButton::Layout && callbacks_.chooseLayout) {
                    const TopBarLayout t = ComputeTopBar(frame.geometry, rect.right - rect.left);
                    callbacks_.chooseLayout({rect.left + (t.layoutButton.left + t.layoutButton.right) / 2,
                                             rect.top + frame.geometry.top});
                }
            }
            return 0;
        case WM_SETCURSOR:
            SetCursor(LoadCursorW(nullptr, dragging_ ? IDC_SIZEALL
                                           : (bar == 0 && frame.hover != FrameButton::None) || hoverShortcut_ >= 0
                                               ? IDC_HAND
                                               : IDC_ARROW));
            return TRUE;
        case WM_ACTIVATE:
        case WM_WINDOWPOSCHANGED: {
            // Appbars are asked to tell the shell about these.
            APPBARDATA data = {sizeof(data)};
            data.hWnd = window;
            SHAppBarMessage(message == WM_ACTIVATE ? ABM_ACTIVATE : ABM_WINDOWPOSCHANGED, &data);
            break;
        }
        case WM_DISPLAYCHANGE:
        case WM_DPICHANGED:
            ScheduleRebuild();
            return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
