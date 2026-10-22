#include "bubbles.h"

#include <algorithm>
#include <cmath>

#include "brand.h"
#include "frame.h"

namespace {

float Clamp01(float x) { return std::clamp(x, 0.0f, 1.0f); }

Rgba Mix(Rgba a, Rgba b, float t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}

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

int LabelSize(float s) { return int(lroundf(11.0f * s)); }

bool Overlaps(const RECT& a, const RECT& b, int gap) {
    return a.left < b.right + gap && b.left < a.right + gap && a.top < b.bottom + gap && b.top < a.bottom + gap;
}

}  // namespace

BubbleMetrics MeasureBubbles(const FrameGeometry& g) {
    BubbleMetrics m;
    if (!g.wide) return m;  // the slim frame is a few pixels wide: no room
    const float s = g.scale;
    const auto px = [s](float v) { return int(lroundf(v * s)); };
    const int room = std::min(g.side, g.top - g.strip) - 2 * px(6);  // the narrowest band, inside its padding
    const int label = px(16) + px(3);
    m.labels = room >= px(60) + label;
    m.diameter = std::min(px(60), m.labels ? room - label : room);
    m.cellWidth = m.labels ? std::min(room, px(88)) : m.diameter;
    m.cellHeight = m.diameter + (m.labels ? label : 0);
    m.gap = px(6);
    m.usable = m.diameter >= px(28);
    return m;
}

RECT BubbleArea(const FrameGeometry& g, const RECT& bar, int band) {
    const LONG pad = LONG(lroundf(6.0f * g.scale));
    const LONG width = bar.right - bar.left;
    const LONG height = bar.bottom - bar.top;
    switch (band) {
        case 0: return {pad, pad, width - pad, (g.top - g.strip) - pad};  // above the strip
        case 3: return {pad, g.radius + pad, width - pad, height - pad};  // below the opening's corners
        default: return {pad, pad, width - pad, height - pad};
    }
}

RECT CellAt(const RECT& area, const BubbleMetrics& m, float fx, float fy) {
    const LONG freeX = std::max(0L, area.right - area.left - m.cellWidth);
    const LONG freeY = std::max(0L, area.bottom - area.top - m.cellHeight);
    const LONG left = area.left + LONG(lroundf(Clamp01(fx) * freeX));
    const LONG top = area.top + LONG(lroundf(Clamp01(fy) * freeY));
    return {left, top, left + m.cellWidth, top + m.cellHeight};
}

void FractionsOf(const RECT& area, const BubbleMetrics& m, POINT centre, float& fx, float& fy) {
    const LONG freeX = std::max(0L, area.right - area.left - m.cellWidth);
    const LONG freeY = std::max(0L, area.bottom - area.top - m.cellHeight);
    fx = freeX ? Clamp01(float(centre.x - m.cellWidth / 2 - area.left) / float(freeX)) : 0.5f;
    fy = freeY ? Clamp01(float(centre.y - m.cellHeight / 2 - area.top) / float(freeY)) : 0.5f;
}

std::vector<RECT> BandSockets(const RECT& area, const BubbleMetrics& m, const std::vector<RECT>& reserved) {
    std::vector<RECT> sockets;
    const bool across = area.right - area.left >= area.bottom - area.top;  // the band's long way
    const LONG length = across ? area.right - area.left : area.bottom - area.top;
    const LONG cell = across ? m.cellWidth : m.cellHeight;
    const LONG spacing = 2 * m.gap;  // at least this much between neighbours
    const LONG count = length >= cell ? (length + spacing) / (cell + spacing) : 0;
    if (count <= 0) return sockets;
    const float pitch = float(length) / float(count);
    const RECT centre = CellAt(area, m, 0.5f, 0.5f);
    for (LONG i = 0; i < count; ++i) {
        const LONG start = (across ? area.left : area.top) + LONG(lroundf(pitch * (i + 0.5f))) - cell / 2;
        RECT socket = centre;
        if (across) {
            socket.left = start;
            socket.right = start + m.cellWidth;
        } else {
            socket.top = start;
            socket.bottom = start + m.cellHeight;
        }
        bool clear = true;
        for (const RECT& other : reserved) clear = clear && !Overlaps(socket, other, m.gap);
        if (clear) sockets.push_back(socket);
    }
    return sockets;
}

std::vector<int> AssignSockets(const std::vector<RECT>& sockets, const std::vector<RECT>& wanted, std::vector<bool>& taken) {
    taken.resize(sockets.size(), false);
    std::vector<int> assigned;
    for (const RECT& want : wanted) {
        const float wx = (want.left + want.right) * 0.5f, wy = (want.top + want.bottom) * 0.5f;
        int best = -1;
        float bestDistance = 0.0f;
        for (size_t i = 0; i < sockets.size(); ++i) {
            if (taken[i]) continue;
            const float dx = (sockets[i].left + sockets[i].right) * 0.5f - wx;
            const float dy = (sockets[i].top + sockets[i].bottom) * 0.5f - wy;
            const float distance = dx * dx + dy * dy;
            if (best < 0 || distance < bestDistance) {
                best = int(i);
                bestDistance = distance;
            }
        }
        if (best >= 0) taken[best] = true;
        assigned.push_back(best);
    }
    return assigned;
}

void PaintSocket(Canvas& canvas, const BubbleMetrics& m, ShortcutKind kind, SocketLook look, float s) {
    canvas.Clear();
    const float cx = m.cellWidth * 0.5f;
    const float cy = m.diameter * 0.5f;
    const float r = m.diameter * 0.5f - 3.0f * s;
    // Apps' sockets are rimmed crimson, folders' sheen blue: the colour says what can go there.
    const Rgba tint = kind == ShortcutKind::App ? venom::kFang : venom::kSheen;
    const float rimStrength = look == SocketLook::Idle ? 0.38f : (look == SocketLook::Open ? 0.8f : 1.0f);
    if (look != SocketLook::Idle)
        canvas.GlowRoundRect(cx - r, cy - r, cx + r, cy + r, r, 3.0f * s,
                             venom::WithAlpha(tint, look == SocketLook::Target ? 0.7f : 0.3f));
    const int x0 = std::max(0, int(cx - r - 1.0f)), x1 = std::min(canvas.Width(), int(cx + r + 2.0f));
    const int y0 = std::max(0, int(cy - r - 1.0f)), y1 = std::min(canvas.Height(), int(cy + r + 2.0f));
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            const float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            const float d = sqrtf(dx * dx + dy * dy);
            const float body = Clamp01(r - d + 0.5f);
            if (body <= 0.0f) continue;
            // A solid, recessed well: darkest at the top-left inner edge, as if lit from above.
            const float edge = Clamp01(d / r);
            const float shade = Clamp01((dx + dy) / (2.0f * r) + 0.5f);  // 0 top-left, 1 bottom-right
            const float lift = edge * edge * shade * 0.06f;
            Rgba base = Mix(venom::kVoid, venom::kHide, 0.4f);
            if (look == SocketLook::Target) base = Mix(base, tint, 0.18f);
            canvas.Blend(x, y, base.r + lift, base.g + lift, base.b + lift * 1.2f, 0.97f * body);
            const float inner = Clamp01(1.0f - fabsf(d - (r - 3.0f * s)) / (2.5f * s)) * (1.0f - shade) * 0.5f;
            if (inner > 0.0f) canvas.Blend(x, y, 0.0f, 0.0f, 0.0f, inner * body);
            const float line = Clamp01(1.0f - fabsf(d - (r - 0.7f * s)) / (0.9f * s));
            if (line > 0.0f) canvas.Blend(x, y, tint.r, tint.g, tint.b, line * body * rimStrength);
        }
    }
    // A small mark in the middle: a dot when idle, a + when it can take the bubble.
    const Rgba mark = venom::WithAlpha(tint, look == SocketLook::Idle ? 0.35f : 0.9f);
    if (look == SocketLook::Idle) {
        canvas.FillCircle(cx, cy, 2.0f * s, mark);
    } else {
        const float arm = r * 0.22f;
        canvas.DrawLine(cx - arm, cy, cx + arm, cy, 2.0f * s, mark);
        canvas.DrawLine(cx, cy - arm, cx, cy + arm, 2.0f * s, mark);
    }
}

bool BubbleHit(const BubbleMetrics& m, POINT local) {
    const float dx = local.x + 0.5f - m.cellWidth * 0.5f;
    const float dy = local.y + 0.5f - m.diameter * 0.5f;
    if (dx * dx + dy * dy <= (m.diameter * 0.5f + 2.0f) * (m.diameter * 0.5f + 2.0f)) return true;
    return m.labels && local.y >= m.diameter && local.y < m.cellHeight && local.x >= 0 && local.x < m.cellWidth;
}

void PaintBubble(Canvas& canvas, const BubbleMetrics& m, const IconImage* icon, const std::wstring& name,
                 ShortcutKind kind, bool hover, float s, BubbleState state) {
    canvas.Clear();
    const float cx = m.cellWidth * 0.5f;
    const float cy = m.diameter * 0.5f;
    const float r = m.diameter * 0.5f - 3.0f * s;  // room for the glow inside the cell
    // Apps glow crimson, folders sheen blue.
    const Rgba rim = kind == ShortcutKind::App ? venom::kFang : venom::kSheen;
    canvas.GlowRoundRect(cx - r, cy - r, cx + r, cy + r, r, 2.5f * s, venom::WithAlpha(rim, hover ? 0.75f : 0.28f));
    const int x0 = std::max(0, int(cx - r - 1.0f)), x1 = std::min(canvas.Width(), int(cx + r + 2.0f));
    const int y0 = std::max(0, int(cy - r - 1.0f)), y1 = std::min(canvas.Height(), int(cy + r + 2.0f));
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            const float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            const float d = sqrtf(dx * dx + dy * dy);
            const float body = Clamp01(r - d + 0.5f);
            if (body <= 0.0f) continue;
            const float edge = Clamp01(d / r);
            const Rgba base = Mix(hover ? venom::kCardHi : venom::kCard, venom::kVoid, edge * edge);
            const float hx = dx + 0.32f * r, hy = dy + 0.42f * r;
            const float shine = expf(-(hx * hx + hy * hy) / (0.10f * r * r)) * 0.10f;
            canvas.Blend(x, y, base.r + shine, base.g + shine, base.b + shine * 1.2f, 0.95f * body);
            const float line = Clamp01(1.0f - fabsf(d - (r - 0.7f * s)) / (0.9f * s));
            if (line > 0.0f) canvas.Blend(x, y, rim.r, rim.g, rim.b, line * body * (hover ? 1.0f : 0.65f));
        }
    }
    if (icon && !icon->Empty()) {
        canvas.DrawPixels(icon->pixels.data(), icon->size, icon->size, int(lroundf(cx - icon->size * 0.5f)),
                          int(lroundf(cy - icon->size * 0.5f)), 1.0f);
    } else {
        std::wstring letter = name.empty() ? L"?" : name.substr(0, 1);
        CharUpperBuffW(letter.data(), DWORD(letter.size()));
        const int size = int(lroundf(r * 0.8f));
        const float width = Canvas::MeasureString(letter, size, FW_BOLD, venom::DisplayFont());
        canvas.DrawString(letter, cx - width * 0.5f, cy, size, FW_BOLD, venom::kBone, venom::DisplayFont());
    }
    // Like the taskbar: a short line under a running app, a long crimson one under the app in front.
    if (state != BubbleState::Closed) {
        const float half = (state == BubbleState::Active ? 8.0f : 3.5f) * s;
        const float y = cy + r + 1.0f * s;
        const Rgba color = state == BubbleState::Active ? venom::kFang : venom::kBone;
        if (state == BubbleState::Active)
            canvas.GlowRoundRect(cx - half, y - 1.5f * s, cx + half, y + 1.5f * s, 1.5f * s, 2.5f * s,
                                 venom::WithAlpha(venom::kFang, 0.6f));
        canvas.FillRoundRect(cx - half, y - 1.5f * s, cx + half, y + 1.5f * s, 1.5f * s, color);
    }
    if (m.labels) {
        const int size = LabelSize(s);
        const std::wstring text = Ellipsize(name, m.cellWidth - 12.0f * s, size, FW_SEMIBOLD, venom::UiFont());
        const float width = Canvas::MeasureString(text, size, FW_SEMIBOLD, venom::UiFont());
        const float top = float(m.diameter) + 3.0f * s, bottom = float(m.cellHeight);
        const float left = cx - width * 0.5f - 6.0f * s, right = cx + width * 0.5f + 6.0f * s;
        canvas.FillRoundRect(left, top, right, bottom, (bottom - top) * 0.5f, venom::WithAlpha(venom::kHide, 0.92f));
        canvas.DrawString(text, cx - width * 0.5f, (top + bottom) * 0.5f, size, FW_SEMIBOLD,
                          hover ? venom::kBone : venom::kBoneDim, venom::UiFont());
    }
}

SIZE BubbleLabelSize(const std::wstring& name, float s) {
    const float width = Canvas::MeasureString(name, int(lroundf(12.0f * s)), FW_SEMIBOLD, venom::UiFont());
    return {LONG(std::min(width, 320.0f * s) + 20.0f * s), LONG(lroundf(26.0f * s))};
}

void PaintBubbleLabel(Canvas& canvas, const std::wstring& name, float s) {
    canvas.Clear();
    const float width = float(canvas.Width()), height = float(canvas.Height());
    canvas.FillRoundRect(0, 0, width, height, height * 0.5f, venom::WithAlpha(venom::kHide, 0.96f));
    canvas.StrokeRoundRect(0, 0, width, height, height * 0.5f, 1.0f * s, venom::WithAlpha(venom::kFang, 0.7f));
    const int size = int(lroundf(12.0f * s));
    const std::wstring text = Ellipsize(name, width - 20.0f * s, size, FW_SEMIBOLD, venom::UiFont());
    const float textWidth = Canvas::MeasureString(text, size, FW_SEMIBOLD, venom::UiFont());
    canvas.DrawString(text, (width - textWidth) * 0.5f, height * 0.5f, size, FW_SEMIBOLD, venom::kBone, venom::UiFont());
}
