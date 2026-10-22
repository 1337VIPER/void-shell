#pragma once

#include <cstdint>
#include <vector>

#include "canvas.h"
#include "common.h"

// Which way the code falls: always from the screen's outer edge toward the desktop.
enum class RainFlow { Down, Up, Right, Left };
enum class RainColor { Venom, Green, Sheen };

// The rain's glyphs (mirrored half-width katakana, digits and a few symbols), rendered once per size into
// coverage masks, in all four orientations so the side bands can fall sideways.
class RainGlyphs {
public:
    void Build(int across, int along);  // cell size for downward flow: width across the lane, height along it
    int Count() const { return count_; }
    int Across() const { return across_; }
    int Along() const { return along_; }
    // A glyph's coverage mask for a flow; Down/Up masks are across x along, Right/Left are along x across.
    const uint8_t* Mask(int glyph, RainFlow flow) const;

private:
    int across_ = 0;
    int along_ = 0;
    int count_ = 0;
    std::vector<uint8_t> masks_[4];
};

// Falling code in one rectangle of a canvas. Drops step a whole cell at a time, like the classic effect.
class RainField {
public:
    void Reset(const RECT& area, RainFlow flow, const RainGlyphs& glyphs, unsigned seed);
    void Step(float dt);
    // Composites the rain over the canvas, only where the canvas is already opaque (the frame's band).
    void Draw(Canvas& canvas, const RainGlyphs& glyphs, RainColor color) const;
    bool Empty() const { return lanes_.empty(); }

private:
    struct Drop {
        float head;   // cells from the outer edge
        float speed;  // cells per second
        int length;
    };
    struct Lane {
        std::vector<uint8_t> cells;  // glyph per cell
        Drop drop;
    };

    float Random();
    void Respawn(Drop& drop, bool anywhere);

    RECT area_ = {};
    RainFlow flow_ = RainFlow::Down;
    int cellAcross_ = 1;
    int cellAlong_ = 1;
    int cellsAlong_ = 0;
    int glyphCount_ = 1;
    uint32_t state_ = 1;
    std::vector<Lane> lanes_;
};
