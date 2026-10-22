#pragma once

#include <vector>

#include "canvas.h"
#include "common.h"
#include "sysinfo.h"

struct FrameGeometry;

// Where the system orbs go on a frame's top bar, in that bar's own pixels. A wide margin gets glass orbs in the
// margin above the strip, with a ring that fills with use; the slim frame, or a margin too small for orbs, gets a
// row of compact chips instead.
struct OrbsLayout {
    RECT box = {};            // the area the orbs are drawn into (bar-local); empty when there's no room
    bool compact = false;
    float radius = 0.0f;      // orbs only
    std::vector<RECT> items;  // each orb's square or chip's pill, relative to box
};

constexpr int kOrbCount = 4;  // CPU, GPU, RAM, VRAM

OrbsLayout LayoutOrbs(const FrameGeometry& geometry, int barWidth);
// Paints into a canvas the size of layout.box: transparent everywhere but the orbs.
void PaintOrbs(Canvas& canvas, const OrbsLayout& layout, const SystemStats& stats, float scale, bool hover);
