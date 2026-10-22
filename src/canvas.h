#pragma once

#include <string>
#include <vector>

#include "common.h"

struct Rgba {
    float r, g, b, a;  // straight (not premultiplied), 0..1
};

// Signed distance from a point to a rounded rectangle: negative inside, positive outside.
float SdRoundRect(float px, float py, float left, float top, float right, float bottom, float radius);

// A premultiplied BGRA bitmap drawn on the CPU and shown with UpdateLayeredWindow, so a window can have soft edges
// and see-through parts. Fully transparent pixels let clicks through to whatever is underneath.
class Canvas {
public:
    Canvas() = default;
    ~Canvas();
    Canvas(const Canvas&) = delete;
    Canvas& operator=(const Canvas&) = delete;

    bool Resize(int width, int height);
    void Clear();
    int Width() const { return width_; }
    int Height() const { return height_; }
    uint32_t* Pixels() { return pixels_; }

    // Composites a colour over one pixel; `a` already includes any coverage.
    void Blend(int x, int y, float r, float g, float b, float a);
    void FillRoundRect(float left, float top, float right, float bottom, float radius, Rgba color);
    void StrokeRoundRect(float left, float top, float right, float bottom, float radius, float width, Rgba color);
    // A soft glow fading out from the outside of a rounded rectangle.
    void GlowRoundRect(float left, float top, float right, float bottom, float radius, float spread, Rgba color);
    void FillCircle(float cx, float cy, float radius, Rgba color);
    // A round-capped line.
    void DrawLine(float x0, float y0, float x1, float y1, float width, Rgba color);
    // Draws single-line text vertically centred on centerY; returns its width in pixels.
    float DrawString(const std::wstring& text, float x, float centerY, int pixelHeight, int weight, Rgba color,
                     const wchar_t* family);
    float DrawString(const std::wstring& text, float x, float centerY, int pixelHeight, bool semibold, Rgba color) {
        return DrawString(text, x, centerY, pixelHeight, semibold ? FW_SEMIBOLD : FW_NORMAL, color, L"Segoe UI");
    }
    static float MeasureString(const std::wstring& text, int pixelHeight, int weight, const wchar_t* family);
    static float MeasureString(const std::wstring& text, int pixelHeight, bool semibold) {
        return MeasureString(text, pixelHeight, semibold ? FW_SEMIBOLD : FW_NORMAL, L"Segoe UI");
    }
    // Copies another canvas of the same size (used to restore a cached background each animation frame).
    void CopyFrom(const Canvas& source);
    // Composites another canvas over this one at (x, y).
    void Draw(const Canvas& source, int x, int y);
    // Composites premultiplied BGRA pixels (an icon) over this one at (x, y), faded by `opacity`.
    void DrawPixels(const uint32_t* pixels, int width, int height, int x, int y, float opacity = 1.0f);
    // Copies the pixels as they are into a window's DC (for ordinary, non-layered windows).
    void Blit(HDC target, int x, int y) const;

    bool Present(HWND window, POINT position, BYTE opacity = 255) const;
    std::vector<uint8_t> Bytes() const;

private:
    void Release();

    HDC dc_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    HGDIOBJ previous_ = nullptr;
    uint32_t* pixels_ = nullptr;
    int width_ = 0;
    int height_ = 0;
};

// The glowing orb used for "open the void" (frame top bar, settings title).
void DrawOrb(Canvas& canvas, float cx, float cy, float radius, float scale, bool lit);
