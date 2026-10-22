#include "rain.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "brand.h"

namespace {

// Half-width katakana (drawn mirrored, as in the films), digits and a few symbols.
constexpr wchar_t kGlyphs[] =
    L"\xFF66\xFF67\xFF68\xFF69\xFF6A\xFF6B\xFF6C\xFF6D\xFF6E\xFF6F\xFF71\xFF72\xFF73\xFF74\xFF75\xFF76\xFF77\xFF78"
    L"\xFF79\xFF7A\xFF7B\xFF7C\xFF7D\xFF7E\xFF7F\xFF80\xFF81\xFF82\xFF83\xFF84\xFF85\xFF86\xFF87\xFF88\xFF89\xFF8A"
    L"\xFF8B\xFF8C\xFF8D\xFF8E\xFF8F\xFF90\xFF91\xFF92\xFF93\xFF94\xFF95\xFF96\xFF97\xFF98\xFF99\xFF9A\xFF9B\xFF9C"
    L"\xFF9D"
    L"0123456789Z:.=*+<>|";

struct Palette {
    Rgba head, body, tail, spark;
};

Palette PaletteFor(RainColor color) {
    switch (color) {
        case RainColor::Green:
            return {{0.85f, 1.0f, 0.90f, 1.0f}, venom::kOk, {0.02f, 0.22f, 0.09f, 1.0f}, venom::kRun};
        case RainColor::Sheen:
            return {{0.88f, 0.92f, 1.0f, 1.0f}, venom::kSheen, {0.05f, 0.07f, 0.30f, 1.0f}, venom::kRun};
        default:
            return {{1.0f, 0.93f, 0.95f, 1.0f}, venom::kFang, {0.30f, 0.03f, 0.08f, 1.0f}, venom::kSheen};
    }
}

}  // namespace

void RainGlyphs::Build(int across, int along) {
    if (across == across_ && along == along_ && count_) return;
    across_ = std::max(4, across);
    along_ = std::max(4, along);
    count_ = int(wcslen(kGlyphs));
    const size_t cell = size_t(across_) * along_;
    for (auto& masks : masks_) masks.assign(cell * count_, 0);

    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = across_;
    info.bmiHeader.biHeight = -along_;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    HDC dc = CreateCompatibleDC(nullptr);
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap) {
        DeleteDC(dc);
        return;
    }
    HGDIOBJ previousBitmap = SelectObject(dc, bitmap);
    HFONT font = CreateFontW(-int(along_ * 0.86f), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, FIXED_PITCH, venom::RainFont());
    HGDIOBJ previousFont = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    const auto* pixels = static_cast<const uint32_t*>(bits);

    for (int g = 0; g < count_; ++g) {
        memset(bits, 0, cell * 4);
        RECT box = {0, 0, across_, along_};
        DrawTextW(dc, &kGlyphs[g], 1, &box, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        GdiFlush();
        uint8_t* down = &masks_[int(RainFlow::Down)][cell * g];
        for (int y = 0; y < along_; ++y)
            for (int x = 0; x < across_; ++x)
                down[size_t(y) * across_ + (across_ - 1 - x)] = uint8_t((pixels[size_t(y) * across_ + x] >> 16) & 255);

        // The other flows are rotations, so each glyph's foot points the way it falls.
        uint8_t* up = &masks_[int(RainFlow::Up)][cell * g];
        uint8_t* right = &masks_[int(RainFlow::Right)][cell * g];  // along_ wide, across_ tall
        uint8_t* left = &masks_[int(RainFlow::Left)][cell * g];
        for (int y = 0; y < along_; ++y) {
            for (int x = 0; x < across_; ++x) {
                const uint8_t c = down[size_t(y) * across_ + x];
                up[size_t(along_ - 1 - y) * across_ + (across_ - 1 - x)] = c;
                right[size_t(across_ - 1 - x) * along_ + y] = c;
                left[size_t(x) * along_ + (along_ - 1 - y)] = c;
            }
        }
    }
    SelectObject(dc, previousFont);
    DeleteObject(font);
    SelectObject(dc, previousBitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);
}

const uint8_t* RainGlyphs::Mask(int glyph, RainFlow flow) const {
    return &masks_[int(flow)][size_t(across_) * along_ * size_t(glyph % std::max(1, count_))];
}

float RainField::Random() {
    // xorshift32: cheap, and deterministic per field so the self-test is repeatable.
    state_ ^= state_ << 13;
    state_ ^= state_ >> 17;
    state_ ^= state_ << 5;
    return float(state_ & 0xFFFFFF) / float(0x1000000);
}

void RainField::Respawn(Drop& drop, bool anywhere) {
    drop.speed = 5.0f + Random() * 8.0f;
    // Trails at least most of the band deep, so every lane reads as a stream rather than a speck.
    drop.length = std::max(4, int(cellsAlong_ * (0.7f + Random() * 0.9f)) + 2);
    // A fresh drop starts just above the band after a short, random pause, so lanes don't move in step.
    drop.head = anywhere ? Random() * (cellsAlong_ + drop.length) : -Random() * (cellsAlong_ * 0.5f + 2.0f);
}

void RainField::Reset(const RECT& area, RainFlow flow, const RainGlyphs& glyphs, unsigned seed) {
    area_ = area;
    flow_ = flow;
    state_ = seed ? seed : 1;
    glyphCount_ = std::max(1, glyphs.Count());
    cellAcross_ = glyphs.Across();
    cellAlong_ = glyphs.Along();
    const bool vertical = flow == RainFlow::Down || flow == RainFlow::Up;
    const int acrossExtent = vertical ? area.right - area.left : area.bottom - area.top;
    const int alongExtent = vertical ? area.bottom - area.top : area.right - area.left;
    cellsAlong_ = std::max(0, alongExtent / cellAlong_);
    const int laneCount = cellsAlong_ > 0 ? std::max(0, acrossExtent / cellAcross_) : 0;
    lanes_.assign(laneCount, Lane());
    for (Lane& lane : lanes_) {
        lane.cells.resize(cellsAlong_);
        for (uint8_t& cell : lane.cells) cell = uint8_t(Random() * glyphCount_);
        Respawn(lane.drop, true);
    }
}

void RainField::Step(float dt) {
    for (Lane& lane : lanes_) {
        lane.drop.head += lane.drop.speed * dt;
        if (lane.drop.head - lane.drop.length > cellsAlong_) Respawn(lane.drop, false);
        // A few glyphs change every step, which gives the shimmer.
        if (Random() < 0.35f && !lane.cells.empty())
            lane.cells[size_t(Random() * lane.cells.size()) % lane.cells.size()] = uint8_t(Random() * glyphCount_);
    }
}

void RainField::Draw(Canvas& canvas, const RainGlyphs& glyphs, RainColor color) const {
    const Palette palette = PaletteFor(color);
    const bool vertical = flow_ == RainFlow::Down || flow_ == RainFlow::Up;
    const int maskWidth = vertical ? cellAcross_ : cellAlong_;
    const int maskHeight = vertical ? cellAlong_ : cellAcross_;
    uint32_t* pixels = canvas.Pixels();
    const int width = canvas.Width();
    const int height = canvas.Height();

    // Composites one glyph cell; premultiplied "over" in integers, the source alpha being coverage x brightness.
    const auto blit = [&](size_t laneIndex, int k, uint8_t glyph, Rgba c, float alpha) {
        int left = 0, top = 0;
        switch (flow_) {
            case RainFlow::Down:
                left = area_.left + int(laneIndex) * cellAcross_;
                top = area_.top + k * cellAlong_;
                break;
            case RainFlow::Up:
                left = area_.left + int(laneIndex) * cellAcross_;
                top = area_.bottom - (k + 1) * cellAlong_;
                break;
            case RainFlow::Right:
                left = area_.left + k * cellAlong_;
                top = area_.top + int(laneIndex) * cellAcross_;
                break;
            case RainFlow::Left:
                left = area_.right - (k + 1) * cellAlong_;
                top = area_.top + int(laneIndex) * cellAcross_;
                break;
        }
        const int sr = int(c.r * 255.0f), sg = int(c.g * 255.0f), sb = int(c.b * 255.0f);
        const int a255 = int(std::min(1.0f, alpha) * 255.0f);
        const uint8_t* mask = glyphs.Mask(glyph, flow_);
        for (int my = 0; my < maskHeight; ++my) {
            const int py = top + my;
            if (py < 0 || py >= height) continue;
            for (int mx = 0; mx < maskWidth; ++mx) {
                const int coverage = mask[my * maskWidth + mx];
                const int px = left + mx;
                if (!coverage || px < 0 || px >= width) continue;
                uint32_t& d = pixels[size_t(py) * width + px];
                const int da = int(d >> 24);
                if (!da) continue;  // keep see-through parts of the frame see-through
                const int a = coverage * a255 / 255;
                const int inv = 255 - a;
                const int r = sr * a / 255 + int((d >> 16) & 255) * inv / 255;
                const int g = sg * a / 255 + int((d >> 8) & 255) * inv / 255;
                const int b = sb * a / 255 + int(d & 255) * inv / 255;
                const int outA = a + da * inv / 255;
                d = (uint32_t(outA) << 24) | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
            }
        }
    };
    // How bright code may be at a depth: it dims as it nears the desktop.
    const auto nearDesktop = [&](int k) {
        const float depth = float(k) / float(std::max(1, cellsAlong_ - 1));
        return std::clamp((1.0f - depth) / 0.3f, 0.3f, 1.0f);
    };

    for (size_t laneIndex = 0; laneIndex < lanes_.size(); ++laneIndex) {
        const Lane& lane = lanes_[laneIndex];
        const int headCell = int(floorf(lane.drop.head));
        const int tailCell = headCell - lane.drop.length;
        for (int k = 0; k < cellsAlong_; ++k) {
            const bool inTrail = k > tailCell && k <= headCell;
            if (!inTrail) {
                // The resting code: every cell keeps a faint glyph, so the margin reads as a wall of code.
                blit(laneIndex, k, lane.cells[k], palette.body, 0.11f * nearDesktop(k));
                continue;
            }
            // The falling stream: brightest at the head, fading along the trail.
            const float t = float(headCell - k) / float(lane.drop.length);
            const float trail = std::max(0.12f, powf(1.0f - t, 1.1f));
            Rgba c;
            if (k == headCell) {
                c = palette.head;
            } else if (((laneIndex * 7 + k * 13 + lane.cells[k]) % 23) == 0) {
                c = palette.spark;  // the odd glyph in the second brand colour
            } else {
                c = {palette.tail.r + (palette.body.r - palette.tail.r) * trail,
                     palette.tail.g + (palette.body.g - palette.tail.g) * trail,
                     palette.tail.b + (palette.body.b - palette.tail.b) * trail, 1.0f};
            }
            blit(laneIndex, k, lane.cells[k], c, trail * nearDesktop(k) * (k == headCell ? 1.0f : 0.9f));
        }
    }
}
