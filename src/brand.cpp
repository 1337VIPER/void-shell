#include "brand.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <vector>

namespace venom {
namespace {

struct Point {
    float x, y;
};
using Path = std::vector<Point>;

// Appends a cubic Bezier (start point already in the path), flattened into short segments.
void Cubic(Path& path, Point c1, Point c2, Point end) {
    const Point start = path.back();
    for (int i = 1; i <= 16; ++i) {
        const float t = i / 16.0f;
        const float u = 1.0f - t;
        const float a = u * u * u, b = 3 * u * u * t, c = 3 * u * t * t, d = t * t * t;
        path.push_back({a * start.x + b * c1.x + c * c2.x + d * end.x, a * start.y + b * c1.y + c * c2.y + d * end.y});
    }
}

// The paths of assets/icon.svg (viewBox 0 0 32 32), converted to absolute coordinates.
const Path& Head() {
    static const Path path = [] {
        Path p = {{16.0f, 1.6f}};
        Cubic(p, {23.4f, 1.6f}, {29.4f, 6.9f}, {29.4f, 15.1f});
        Cubic(p, {29.4f, 22.3f}, {24.8f, 28.3f}, {19.6f, 30.0f});
        p.push_back({18.3f, 26.3f});  // the fang notch
        p.push_back({16.0f, 30.5f});
        p.push_back({13.7f, 26.3f});
        p.push_back({12.4f, 30.0f});
        Cubic(p, {7.2f, 28.3f}, {2.6f, 22.3f}, {2.6f, 15.1f});
        Cubic(p, {2.6f, 6.9f}, {8.6f, 1.6f}, {16.0f, 1.6f});
        return p;
    }();
    return path;
}

const Path& Sheen() {
    static const Path path = [] {
        Path p = {{8.2f, 6.2f}};
        Cubic(p, {10.5f, 4.5f}, {13.2f, 3.6f}, {16.0f, 3.6f});
        return p;
    }();
    return path;
}

const Path& Eye(bool right) {
    static const Path left = [] {
        Path p = {{5.6f, 12.2f}};
        Cubic(p, {9.5f, 11.5f}, {13.2f, 13.5f}, {15.0f, 17.3f});
        Cubic(p, {10.9f, 18.4f}, {7.2f, 16.6f}, {5.6f, 12.2f});
        return p;
    }();
    static const Path mirrored = [] {
        Path p;
        for (const Point& q : left) p.push_back({32.0f - q.x, q.y});
        return p;
    }();
    return right ? mirrored : left;
}

float SegmentDistance(Point p, Point a, Point b) {
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float t = std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / std::max(dx * dx + dy * dy, 1e-6f), 0.0f, 1.0f);
    const float ex = p.x - a.x - dx * t, ey = p.y - a.y - dy * t;
    return sqrtf(ex * ex + ey * ey);
}

// Signed distance to a closed path (negative inside), in path units.
float SignedDistance(const Path& path, Point p) {
    float best = 1e9f;
    bool inside = false;
    for (size_t i = 0, j = path.size() - 1; i < path.size(); j = i++) {
        const Point& a = path[i];
        const Point& b = path[j];
        best = std::min(best, SegmentDistance(p, a, b));
        if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x) inside = !inside;
    }
    return inside ? -best : best;
}

float OpenDistance(const Path& path, Point p) {
    float best = 1e9f;
    for (size_t i = 1; i < path.size(); ++i) best = std::min(best, SegmentDistance(p, path[i - 1], path[i]));
    return best;
}

bool Installed(const wchar_t* family) {
    LOGFONTW font = {};
    font.lfCharSet = DEFAULT_CHARSET;
    wcsncpy_s(font.lfFaceName, family, _TRUNCATE);
    bool found = false;
    HDC dc = GetDC(nullptr);
    EnumFontFamiliesExW(
        dc, &font,
        [](const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM found) -> int {
            *reinterpret_cast<bool*>(found) = true;
            return 0;
        },
        reinterpret_cast<LPARAM>(&found), 0);
    ReleaseDC(nullptr, dc);
    return found;
}

const wchar_t* FirstInstalled(std::initializer_list<const wchar_t*> families) {
    for (const wchar_t* family : families)
        if (Installed(family)) return family;
    return L"Segoe UI";
}

}  // namespace

const wchar_t* DisplayFont() {
    static const wchar_t* font = FirstInstalled({L"Unbounded", L"Segoe UI Variable Display", L"Segoe UI"});
    return font;
}

const wchar_t* UiFont() {
    static const wchar_t* font = FirstInstalled({L"Manrope", L"Segoe UI Variable Text", L"Segoe UI"});
    return font;
}

const wchar_t* MonoFont() {
    static const wchar_t* font = FirstInstalled({L"JetBrains Mono", L"Cascadia Mono", L"Consolas"});
    return font;
}

const wchar_t* RainFont() {
    static const wchar_t* font = FirstInstalled({L"MS Gothic", L"Yu Gothic UI", L"Consolas"});
    return font;
}

const wchar_t* IconFont() {
    static const wchar_t* font = FirstInstalled({L"Segoe Fluent Icons", L"Segoe MDL2 Assets"});
    return font;
}

void DrawMark(Canvas& canvas, float cx, float cy, float size, MarkStyle style, bool lit) {
    // Map icon units to pixels: the head spans about 29 units from top to the fang tips.
    const float k = size / 29.5f;
    const float originX = 16.0f;
    const float originY = 16.05f;
    const bool glow = style == MarkStyle::Glow;
    const float edgeWidth = glow ? 1.7f : 0.8f;
    const float glowReach = glow ? 3.0f : 0.0f;
    const float half = size * 0.5f + (edgeWidth + glowReach) * k + 2.0f;

    const int x0 = std::max(0, int(floorf(cx - half)));
    const int y0 = std::max(0, int(floorf(cy - half)));
    const int x1 = std::min(canvas.Width(), int(ceilf(cx + half)));
    const int y1 = std::min(canvas.Height(), int(ceilf(cy + half)));
    const Rgba edge = glow ? kFang : kMarkStroke;
    const float pixel = 1.0f / k;  // one pixel in icon units, for anti-aliasing
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            const Point p = {(x + 0.5f - cx) / k + originX, (y + 0.5f - cy) / k + originY};
            const float head = SignedDistance(Head(), p);
            const auto cover = [&](float d) { return std::clamp(0.5f - d / pixel, 0.0f, 1.0f); };
            if (glow && head > edgeWidth * 0.5f) {
                const float falloff = expf(-(head - edgeWidth * 0.5f) / (glowReach * 0.45f));
                canvas.Blend(x, y, kFang.r, kFang.g, kFang.b, (lit ? 0.55f : 0.32f) * falloff);
            }
            // Body, then its edge, then the sheen arc and the eyes on top.
            canvas.Blend(x, y, kMarkFill.r, kMarkFill.g, kMarkFill.b, cover(head));
            const float edgeCover = cover(fabsf(head) - edgeWidth * 0.5f);
            if (edgeCover > 0.0f) canvas.Blend(x, y, edge.r, edge.g, edge.b, edgeCover);
            const float arc = cover(OpenDistance(Sheen(), p) - 0.7f);
            if (arc > 0.0f) canvas.Blend(x, y, kSheen.r, kSheen.g, kSheen.b, arc);
            const float eyes = std::max(cover(SignedDistance(Eye(false), p)), cover(SignedDistance(Eye(true), p)));
            if (eyes > 0.0f) canvas.Blend(x, y, kBone.r, kBone.g, kBone.b, eyes);
        }
    }
}

HICON CreateMarkIcon(int size) {
    Canvas canvas;
    if (!canvas.Resize(size, size)) return LoadIconW(nullptr, IDI_APPLICATION);
    DrawMark(canvas, size * 0.5f, size * 0.5f, size * 0.86f, MarkStyle::Glow, true);

    // Icons want straight (not premultiplied) alpha.
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = size;
    info.bmiHeader.biHeight = -size;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HDC screen = GetDC(nullptr);
    HBITMAP color = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, screen);
    if (!color) return LoadIconW(nullptr, IDI_APPLICATION);
    auto* out = static_cast<uint32_t*>(bits);
    for (int i = 0; i < size * size; ++i) {
        const uint32_t p = canvas.Pixels()[i];
        const uint32_t a = p >> 24;
        const auto unmultiply = [a](uint32_t c) { return a ? std::min(255u, c * 255u / a) : 0u; };
        out[i] = (a << 24) | (unmultiply((p >> 16) & 255) << 16) | (unmultiply((p >> 8) & 255) << 8) | unmultiply(p & 255);
    }
    std::vector<uint8_t> maskBits(size_t((size + 15) / 16) * 2 * size, 0);
    HBITMAP mask = CreateBitmap(size, size, 1, 1, maskBits.data());
    ICONINFO iconInfo = {TRUE, 0, 0, mask, color};
    HICON icon = CreateIconIndirect(&iconInfo);
    DeleteObject(mask);
    DeleteObject(color);
    return icon ? icon : LoadIconW(nullptr, IDI_APPLICATION);
}

}  // namespace venom
