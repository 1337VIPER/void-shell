#include "canvas.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

float Clamp01(float x) { return std::clamp(x, 0.0f, 1.0f); }

uint32_t Pack(float a, float r, float g, float b) {
    const auto channel = [](float v) { return uint32_t(Clamp01(v) * 255.0f + 0.5f); };
    return (channel(a) << 24) | (channel(r) << 16) | (channel(g) << 8) | channel(b);
}

}  // namespace

float SdRoundRect(float px, float py, float left, float top, float right, float bottom, float radius) {
    const float cx = (left + right) * 0.5f;
    const float cy = (top + bottom) * 0.5f;
    const float qx = fabsf(px - cx) - ((right - left) * 0.5f - radius);
    const float qy = fabsf(py - cy) - ((bottom - top) * 0.5f - radius);
    const float ox = std::max(qx, 0.0f);
    const float oy = std::max(qy, 0.0f);
    return sqrtf(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.0f) - radius;
}

Canvas::~Canvas() { Release(); }

void Canvas::Release() {
    if (dc_) {
        SelectObject(dc_, previous_);
        DeleteDC(dc_);
    }
    if (bitmap_) DeleteObject(bitmap_);
    dc_ = nullptr;
    bitmap_ = nullptr;
    previous_ = nullptr;
    pixels_ = nullptr;
    width_ = height_ = 0;
}

bool Canvas::Resize(int width, int height) {
    if (width == width_ && height == height_ && pixels_) return true;
    Release();
    if (width <= 0 || height <= 0) return false;
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;  // top-down
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    dc_ = CreateCompatibleDC(nullptr);
    void* bits = nullptr;
    bitmap_ = CreateDIBSection(dc_, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap_) {
        Release();
        return false;
    }
    previous_ = SelectObject(dc_, bitmap_);
    pixels_ = static_cast<uint32_t*>(bits);
    width_ = width;
    height_ = height;
    Clear();
    return true;
}

void Canvas::Clear() {
    if (pixels_) memset(pixels_, 0, size_t(width_) * height_ * 4);
}

void Canvas::Blend(int x, int y, float r, float g, float b, float a) {
    if (x < 0 || y < 0 || x >= width_ || y >= height_ || a <= 0.0f) return;
    a = std::min(a, 1.0f);
    uint32_t& pixel = pixels_[size_t(y) * width_ + x];
    const float inverse = 1.0f - a;
    const float da = float(pixel >> 24) / 255.0f;
    const float dr = float((pixel >> 16) & 255) / 255.0f;
    const float dg = float((pixel >> 8) & 255) / 255.0f;
    const float db = float(pixel & 255) / 255.0f;
    pixel = Pack(a + da * inverse, r * a + dr * inverse, g * a + dg * inverse, b * a + db * inverse);
}

void Canvas::FillRoundRect(float left, float top, float right, float bottom, float radius, Rgba color) {
    const int x0 = std::max(0, int(floorf(left)));
    const int y0 = std::max(0, int(floorf(top)));
    const int x1 = std::min(width_, int(ceilf(right)));
    const int y1 = std::min(height_, int(ceilf(bottom)));
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            const float d = SdRoundRect(x + 0.5f, y + 0.5f, left, top, right, bottom, radius);
            const float coverage = Clamp01(0.5f - d);
            if (coverage > 0.0f) Blend(x, y, color.r, color.g, color.b, color.a * coverage);
        }
    }
}

void Canvas::StrokeRoundRect(float left, float top, float right, float bottom, float radius, float width,
                             Rgba color) {
    // The stroke sits just inside the edge.
    const float half = width * 0.5f;
    const int x0 = std::max(0, int(floorf(left)));
    const int y0 = std::max(0, int(floorf(top)));
    const int x1 = std::min(width_, int(ceilf(right)));
    const int y1 = std::min(height_, int(ceilf(bottom)));
    const float band = width + radius + 2.0f;
    for (int y = y0; y < y1; ++y) {
        const bool edgeRow = y < top + band || y > bottom - band;
        for (int x = x0; x < x1; ++x) {
            // Rows away from the top and bottom only touch the stroke near the left and right edges.
            if (!edgeRow && x > left + band && x < right - band) {
                x = int(right - band);
                continue;
            }
            const float d = SdRoundRect(x + 0.5f, y + 0.5f, left, top, right, bottom, radius);
            const float coverage = Clamp01(half + 0.5f - fabsf(d + half));
            if (coverage > 0.0f) Blend(x, y, color.r, color.g, color.b, color.a * coverage);
        }
    }
}

void Canvas::GlowRoundRect(float left, float top, float right, float bottom, float radius, float spread,
                           Rgba color) {
    const float reach = spread * 3.0f;
    const int x0 = std::max(0, int(floorf(left - reach)));
    const int y0 = std::max(0, int(floorf(top - reach)));
    const int x1 = std::min(width_, int(ceilf(right + reach)));
    const int y1 = std::min(height_, int(ceilf(bottom + reach)));
    for (int y = y0; y < y1; ++y) {
        const bool edgeRow = y < top + radius || y > bottom - radius;
        for (int x = x0; x < x1; ++x) {
            // Inside the rectangle there is no glow, so skip across it.
            if (!edgeRow && x > left && x < right) {
                x = std::max(x, int(right) - 1);  // never backwards: with a fractional edge that would loop forever
                continue;
            }
            const float d = SdRoundRect(x + 0.5f, y + 0.5f, left, top, right, bottom, radius);
            if (d <= 0.0f || d > reach) continue;
            Blend(x, y, color.r, color.g, color.b, color.a * expf(-d / spread));
        }
    }
}

void Canvas::FillCircle(float cx, float cy, float radius, Rgba color) {
    FillRoundRect(cx - radius, cy - radius, cx + radius, cy + radius, radius, color);
}

void Canvas::DrawLine(float x0, float y0, float x1, float y1, float width, Rgba color) {
    const float half = width * 0.5f;
    const float dx = x1 - x0;
    const float dy = y1 - y0;
    const float lengthSquared = std::max(dx * dx + dy * dy, 1e-6f);
    const int left = std::max(0, int(floorf(std::min(x0, x1) - half - 1.0f)));
    const int top = std::max(0, int(floorf(std::min(y0, y1) - half - 1.0f)));
    const int right = std::min(width_, int(ceilf(std::max(x0, x1) + half + 1.0f)));
    const int bottom = std::min(height_, int(ceilf(std::max(y0, y1) + half + 1.0f)));
    for (int y = top; y < bottom; ++y) {
        for (int x = left; x < right; ++x) {
            const float px = x + 0.5f - x0;
            const float py = y + 0.5f - y0;
            const float t = Clamp01((px * dx + py * dy) / lengthSquared);
            const float ex = px - dx * t;
            const float ey = py - dy * t;
            const float coverage = Clamp01(half + 0.5f - sqrtf(ex * ex + ey * ey));
            if (coverage > 0.0f) Blend(x, y, color.r, color.g, color.b, color.a * coverage);
        }
    }
}

void Canvas::CopyFrom(const Canvas& source) {
    if (source.width_ == width_ && source.height_ == height_ && pixels_ && source.pixels_)
        memcpy(pixels_, source.pixels_, size_t(width_) * height_ * 4);
}

float Canvas::MeasureString(const std::wstring& text, int pixelHeight, int weight, const wchar_t* family) {
    HDC dc = CreateCompatibleDC(nullptr);
    HFONT font = CreateFontW(-pixelHeight, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS,
                             CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, family);
    HGDIOBJ previous = SelectObject(dc, font);
    SIZE size = {};
    GetTextExtentPoint32W(dc, text.c_str(), int(text.size()), &size);
    SelectObject(dc, previous);
    DeleteObject(font);
    DeleteDC(dc);
    return float(size.cx);
}

void DrawOrb(Canvas& canvas, float cx, float cy, float r, float s, bool lit) {
    canvas.GlowRoundRect(cx - r, cy - r, cx + r, cy + r, r, 2.5f * s, {0.30f, 0.60f, 1.0f, lit ? 0.85f : 0.5f});
    canvas.FillCircle(cx, cy, r, {0.10f, 0.26f, 0.70f, 1.0f});
    canvas.FillCircle(cx - 0.28f * r, cy - 0.28f * r, 0.6f * r, {0.40f, 0.78f, 1.0f, 0.7f});
    canvas.StrokeRoundRect(cx - r, cy - r, cx + r, cy + r, r, 1.2f * s, {0.60f, 0.88f, 1.0f, 0.9f});
}

float Canvas::DrawString(const std::wstring& text, float x, float centerY, int pixelHeight, int weight, Rgba color,
                         const wchar_t* family) {
    if (text.empty()) return 0.0f;
    HDC dc = CreateCompatibleDC(nullptr);
    HFONT font = CreateFontW(-pixelHeight, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS,
                             CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, family);
    HGDIOBJ previousFont = SelectObject(dc, font);
    RECT measure = {0, 0, 0, 0};
    DrawTextW(dc, text.c_str(), -1, &measure, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
    const int width = measure.right;
    const int height = measure.bottom;

    // White text on black in a scratch bitmap: the red channel is the glyph coverage.
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP mask = width > 0 && height > 0 ? CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0) : nullptr;
    if (mask) {
        HGDIOBJ previousBitmap = SelectObject(dc, mask);
        memset(bits, 0, size_t(width) * height * 4);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 255, 255));
        RECT area = {0, 0, width, height};
        DrawTextW(dc, text.c_str(), -1, &area, DT_SINGLELINE | DT_NOPREFIX);
        GdiFlush();
        const auto* coverage = static_cast<const uint32_t*>(bits);
        const int left = int(lroundf(x));
        const int top = int(lroundf(centerY - height * 0.5f));
        for (int y = 0; y < height; ++y) {
            for (int column = 0; column < width; ++column) {
                const float c = float((coverage[size_t(y) * width + column] >> 16) & 255) / 255.0f;
                if (c > 0.0f) Blend(left + column, top + y, color.r, color.g, color.b, color.a * c);
            }
        }
        SelectObject(dc, previousBitmap);
        DeleteObject(mask);
    }
    SelectObject(dc, previousFont);
    DeleteObject(font);
    DeleteDC(dc);
    return float(width);
}

void Canvas::Draw(const Canvas& source, int x, int y) {
    for (int row = 0; row < source.height_; ++row) {
        const int ty = y + row;
        if (ty < 0 || ty >= height_) continue;
        for (int column = 0; column < source.width_; ++column) {
            const int tx = x + column;
            if (tx < 0 || tx >= width_) continue;
            const uint32_t s = source.pixels_[size_t(row) * source.width_ + column];
            const float sa = float(s >> 24) / 255.0f;
            if (sa <= 0.0f) continue;
            uint32_t& d = pixels_[size_t(ty) * width_ + tx];
            const float inverse = 1.0f - sa;
            // Both are premultiplied, so "over" is a plain weighted sum.
            const auto mix = [&](int shift) {
                return float((s >> shift) & 255) / 255.0f + float((d >> shift) & 255) / 255.0f * inverse;
            };
            d = Pack(mix(24), mix(16), mix(8), mix(0));
        }
    }
}

void Canvas::DrawPixels(const uint32_t* source, int width, int height, int x, int y, float opacity) {
    if (!pixels_ || !source) return;
    const uint32_t fade = uint32_t(Clamp01(opacity) * 256.0f);
    for (int row = 0; row < height; ++row) {
        const int ty = y + row;
        if (ty < 0 || ty >= height_) continue;
        for (int column = 0; column < width; ++column) {
            const int tx = x + column;
            if (tx < 0 || tx >= width_) continue;
            uint32_t s = source[size_t(row) * width + column];
            if (fade < 256) {
                s = ((((s >> 24) & 255) * fade >> 8) << 24) | ((((s >> 16) & 255) * fade >> 8) << 16) |
                    ((((s >> 8) & 255) * fade >> 8) << 8) | ((s & 255) * fade >> 8);
            }
            const uint32_t sa = s >> 24;
            if (!sa) continue;
            uint32_t& d = pixels_[size_t(ty) * width_ + tx];
            const uint32_t inverse = 255 - sa;
            // Premultiplied "over", in integers.
            const auto mix = [&](int shift) {
                return std::min(255u, ((s >> shift) & 255) + (((d >> shift) & 255) * inverse + 127) / 255);
            };
            d = (mix(24) << 24) | (mix(16) << 16) | (mix(8) << 8) | mix(0);
        }
    }
}

void Canvas::Blit(HDC target, int x, int y) const {
    if (dc_) BitBlt(target, x, y, width_, height_, dc_, 0, 0, SRCCOPY);
}

bool Canvas::Present(HWND window, POINT position, BYTE opacity) const {
    if (!dc_) return false;
    SIZE size = {width_, height_};
    POINT source = {0, 0};
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, opacity, AC_SRC_ALPHA};
    return UpdateLayeredWindow(window, nullptr, &position, &size, dc_, &source, 0, &blend, ULW_ALPHA) != FALSE;
}

std::vector<uint8_t> Canvas::Bytes() const {
    std::vector<uint8_t> bytes(size_t(width_) * height_ * 4);
    if (pixels_) memcpy(bytes.data(), pixels_, bytes.size());
    return bytes;
}
