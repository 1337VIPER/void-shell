#include "stat_orbs.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "brand.h"
#include "frame.h"

namespace {

constexpr float kTwoPi = 6.2831853f;
constexpr Rgba kWarm = {1.0f, 0.82f, 0.24f, 1.0f};  // --warn #ffd23d

float Clamp01(float x) { return std::clamp(x, 0.0f, 1.0f); }

Rgba Mix(Rgba a, Rgba b, float t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}

// A load's colour: sheen blue when idle, through violet, to crimson when flat out.
Rgba LoadColor(float percent) { return Mix(venom::kSheen, venom::kFang, Clamp01(percent / 100.0f)); }

Rgba TempColor(float celsius) {
    if (celsius < 0.0f) return venom::kBoneFaint;
    if (celsius < 65.0f) return venom::WithAlpha(venom::kOk, 0.9f);
    if (celsius < 82.0f) return kWarm;
    return venom::kFang;
}

void Centered(Canvas& canvas, const std::wstring& text, float cx, float cy, int size, int weight, Rgba color,
              const wchar_t* family) {
    const float width = Canvas::MeasureString(text, size, weight, family);
    canvas.DrawString(text, cx - width * 0.5f, cy, size, weight, color, family);
}

std::wstring Percent(float value) {
    return value < 0.0f ? L"—" : std::to_wstring(int(lroundf(value))) + L"%";
}

std::wstring Temp(float celsius) {
    return celsius < 0.0f ? L"—°C" : std::to_wstring(int(lroundf(celsius))) + L"°C";
}

std::wstring Gigabytes(float used, float total) {
    if (total <= 0.0f) return L"—";
    wchar_t text[48];
    swprintf_s(text, L"%.1f/%.0f GB", used, total);
    return text;
}

struct Gauge {
    const wchar_t* label;
    float value;      // percent, or -1
    std::wstring sub;
    Rgba subColor;
    bool temp;        // the sub line is a temperature (chips show only those)
};

std::vector<Gauge> Gauges(const SystemStats& s) {
    const Rgba dim = venom::kBoneDim;
    return {
        {L"CPU", s.cpu, Temp(s.cpuTemp), TempColor(s.cpuTemp), true},
        {L"GPU", s.gpu, Temp(s.gpuTemp), TempColor(s.gpuTemp), true},
        {L"RAM", s.ram, Gigabytes(s.ramUsedGb, s.ramTotalGb), dim, false},
        {L"VRAM", s.vram, Gigabytes(s.vramUsedGb, s.vramTotalGb), dim, false},
    };
}

// A ring that fills clockwise from the top, its colour running blue to crimson along the way.
void PaintRing(Canvas& canvas, float cx, float cy, float radius, float width, float percent) {
    const float half = width * 0.5f;
    const float fill = percent < 0.0f ? 0.0f : Clamp01(percent / 100.0f);
    const int x0 = std::max(0, int(cx - radius - half - 1.0f)), x1 = std::min(canvas.Width(), int(cx + radius + half + 2.0f));
    const int y0 = std::max(0, int(cy - radius - half - 1.0f)), y1 = std::min(canvas.Height(), int(cy + radius + half + 2.0f));
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            const float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            const float d = sqrtf(dx * dx + dy * dy);
            const float coverage = Clamp01(half + 0.5f - fabsf(d - radius));
            if (coverage <= 0.0f) continue;
            canvas.Blend(x, y, venom::kCardHi.r, venom::kCardHi.g, venom::kCardHi.b, coverage);  // the track
            if (fill <= 0.0f) continue;
            float angle = atan2f(dx, -dy);  // 0 at the top, growing clockwise
            if (angle < 0.0f) angle += kTwoPi;
            const float along = angle / kTwoPi;
            // Soft end: how far past the end of the fill this pixel is, in pixels along the ring.
            const float past = (along - fill) * kTwoPi * radius;
            const float arc = coverage * Clamp01(0.5f - past);
            if (arc <= 0.0f) continue;
            const Rgba color = LoadColor(along * 100.0f);
            canvas.Blend(x, y, color.r, color.g, color.b, arc);
        }
    }
}

void PaintOrb(Canvas& canvas, float cx, float cy, float r, float s, const Gauge& gauge, bool hover) {
    const Rgba tint = gauge.value < 0.0f ? venom::kBoneFaint : LoadColor(gauge.value);
    canvas.GlowRoundRect(cx - r, cy - r, cx + r, cy + r, r, 3.5f * s, venom::WithAlpha(tint, hover ? 0.6f : 0.32f));
    // Glass: a dark bubble, lit faintly from the top left, with a rim in its load colour.
    const int x0 = std::max(0, int(cx - r - 1.0f)), x1 = std::min(canvas.Width(), int(cx + r + 2.0f));
    const int y0 = std::max(0, int(cy - r - 1.0f)), y1 = std::min(canvas.Height(), int(cy + r + 2.0f));
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            const float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            const float d = sqrtf(dx * dx + dy * dy);
            const float body = Clamp01(r - d + 0.5f);
            if (body <= 0.0f) continue;
            const float edge = Clamp01(d / r);
            const Rgba base = Mix(venom::kCard, venom::kVoid, edge * edge);
            const float hx = dx + 0.32f * r, hy = dy + 0.42f * r;
            const float shine = expf(-(hx * hx + hy * hy) / (0.10f * r * r)) * 0.09f;
            canvas.Blend(x, y, base.r + shine, base.g + shine, base.b + shine * 1.2f, 0.94f * body);
            const float rim = Clamp01(1.0f - fabsf(d - (r - 0.7f * s)) / (0.9f * s));
            if (rim > 0.0f) canvas.Blend(x, y, tint.r, tint.g, tint.b, rim * body * (hover ? 0.95f : 0.6f));
        }
    }
    PaintRing(canvas, cx, cy, r - 6.0f * s, 3.6f * s, gauge.value);

    const auto px = [](float v) { return std::max(7, int(lroundf(v))); };
    Centered(canvas, gauge.label, cx, cy - 0.37f * r, px(0.21f * r), FW_BOLD, venom::kBoneDim, venom::DisplayFont());
    Centered(canvas, Percent(gauge.value), cx, cy + 0.02f * r, px(0.40f * r), FW_BOLD,
             gauge.value < 0.0f ? venom::kBoneFaint : venom::kBone, venom::DisplayFont());
    Centered(canvas, gauge.sub, cx, cy + 0.40f * r, px(0.21f * r), FW_SEMIBOLD, gauge.subColor, venom::UiFont());
}

// Chip text sizes, shared by the layout (worst-case widths) and the painting.
int ChipLabelSize(float s) { return int(lroundf(10.0f * s)); }
int ChipValueSize(float s) { return int(lroundf(12.0f * s)); }

void PaintChip(Canvas& canvas, const RECT& chip, float s, const Gauge& gauge, bool hover) {
    const float h = float(chip.bottom - chip.top);
    const float cy = (chip.top + chip.bottom) * 0.5f;
    canvas.FillRoundRect(float(chip.left), float(chip.top), float(chip.right), float(chip.bottom), h * 0.5f,
                         venom::WithAlpha(venom::kHide, 0.94f));
    canvas.StrokeRoundRect(float(chip.left), float(chip.top), float(chip.right), float(chip.bottom), h * 0.5f, 1.0f * s,
                           hover ? venom::WithAlpha(venom::kFang, 0.8f) : venom::kCardHi);
    const float ringR = h * 0.5f - 5.0f * s;
    float x = chip.left + 5.0f * s + ringR + 1.0f * s;
    PaintRing(canvas, x, cy, ringR, 2.4f * s, gauge.value);
    x += ringR + 6.0f * s;
    x += canvas.DrawString(gauge.label, x, cy, ChipLabelSize(s), FW_BOLD, venom::kBoneDim, venom::DisplayFont());
    x += 5.0f * s;
    x += canvas.DrawString(Percent(gauge.value), x, cy, ChipValueSize(s), FW_BOLD,
                           gauge.value < 0.0f ? venom::kBoneFaint : venom::kBone, venom::UiFont());
    if (gauge.temp) {
        x += 6.0f * s;
        canvas.DrawString(gauge.sub, x, cy, ChipLabelSize(s), FW_SEMIBOLD, gauge.subColor, venom::UiFont());
    }
}

}  // namespace

OrbsLayout LayoutOrbs(const FrameGeometry& g, int barWidth) {
    const float s = g.scale;
    const auto px = [s](float v) { return LONG(lroundf(v * s)); };
    OrbsLayout layout;
    const LONG pad = px(9);  // room for the glow
    // Wide: the margin above the strip, where the rain falls. Slim: the strip itself.
    const LONG areaTop = 0;
    const LONG areaBottom = g.wide ? g.top - g.strip : g.top;
    const LONG area = areaBottom - areaTop;
    const LONG diameter = std::min(area - 2 * pad, px(88));
    if (g.wide && diameter >= px(62)) {
        const LONG gap = px(20);
        const LONG row = kOrbCount * diameter + (kOrbCount - 1) * gap;
        const LONG left = (barWidth - row) / 2 - pad;
        const LONG top = areaTop + (area - diameter) / 2 - pad;
        layout.box = {left, top, left + row + 2 * pad, top + diameter + 2 * pad};
        layout.radius = diameter * 0.5f;
        for (int i = 0; i < kOrbCount; ++i) {
            const LONG x = pad + i * (diameter + gap);
            layout.items.push_back({x, pad, x + diameter, pad + diameter});
        }
        return layout;
    }
    // Chips: one line each, sized for their widest possible text so they never jump about. They have no glow, so
    // they need little room around them.
    layout.compact = true;
    const LONG chipPad = px(3);
    const LONG height = std::min(px(22), area - px(8));
    if (height < px(16)) return layout;  // no room at all
    const float labelWidth = Canvas::MeasureString(L"VRAM", ChipLabelSize(s), FW_BOLD, venom::DisplayFont());
    const float valueWidth = Canvas::MeasureString(L"100%", ChipValueSize(s), FW_BOLD, venom::UiFont());
    const float tempWidth = Canvas::MeasureString(L"100°C", ChipLabelSize(s), FW_SEMIBOLD, venom::UiFont());
    const LONG plain = LONG(ceilf(height + 6.0f * s + labelWidth + 5.0f * s + valueWidth + 12.0f * s));
    const LONG withTemp = plain + LONG(ceilf(6.0f * s + tempWidth));
    const LONG gap = px(8);
    const LONG row = 2 * withTemp + 2 * plain + 3 * gap;
    const LONG left = (barWidth - row) / 2 - chipPad;
    const LONG top = areaTop + (area - height) / 2 - chipPad;
    layout.box = {left, top, left + row + 2 * chipPad, top + height + 2 * chipPad};
    LONG x = chipPad;
    for (int i = 0; i < kOrbCount; ++i) {
        const LONG width = i < 2 ? withTemp : plain;
        layout.items.push_back({x, chipPad, x + width, chipPad + height});
        x += width + gap;
    }
    return layout;
}

void PaintOrbs(Canvas& canvas, const OrbsLayout& layout, const SystemStats& stats, float scale, bool hover) {
    canvas.Clear();
    const std::vector<Gauge> gauges = Gauges(stats);
    for (size_t i = 0; i < layout.items.size() && i < gauges.size(); ++i) {
        const RECT& item = layout.items[i];
        if (layout.compact) {
            PaintChip(canvas, item, scale, gauges[i], hover);
        } else {
            PaintOrb(canvas, (item.left + item.right) * 0.5f, (item.top + item.bottom) * 0.5f, layout.radius, scale,
                     gauges[i], hover);
        }
    }
}
