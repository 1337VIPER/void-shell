#pragma once

#include <string>
#include <vector>

#include "canvas.h"
#include "common.h"
#include "icons.h"
#include "settings.h"

struct FrameGeometry;

// Shortcut bubbles in the frame's wide margin: apps sit in sockets down the side bands, folders in sockets along the
// top and bottom bands. Everything here is layout and painting only, in a bar's own pixels, so it can be checked
// without windows.

struct BubbleMetrics {
    int diameter = 0;  // the round bubble
    int cellWidth = 0;  // bubble plus its name underneath, if there's room for one
    int cellHeight = 0;
    bool labels = false;
    int gap = 0;        // kept clear between bubbles
    bool usable = false;  // false for the slim frame: no room
};

BubbleMetrics MeasureBubbles(const FrameGeometry& geometry);
// Where bubble cells may sit in a bar (bar-local): inside the band, clear of the desktop opening's corners and,
// in the top bar, above the strip.
RECT BubbleArea(const FrameGeometry& geometry, const RECT& barRect, int band);
// The cell for a shortcut's stored position, and the stored position for a cell centre.
RECT CellAt(const RECT& area, const BubbleMetrics& metrics, float fx, float fy);
void FractionsOf(const RECT& area, const BubbleMetrics& metrics, POINT centre, float& fx, float& fy);
// A band's sockets: as many cells as fit along it, spread so the spaces between them are equal, centred across the
// band. Sockets that would overlap a reserved rectangle (the orbs, the dock) are left out.
std::vector<RECT> BandSockets(const RECT& area, const BubbleMetrics& metrics, const std::vector<RECT>& reserved);
// Gives each wanted cell, in order, the nearest socket not yet taken (and marks it taken); -1 once all are taken.
std::vector<int> AssignSockets(const std::vector<RECT>& sockets, const std::vector<RECT>& wanted, std::vector<bool>& taken);

// Idle: an empty socket. Open: it can take the bubble being dragged. Target: where that bubble will land.
enum class SocketLook { Idle, Open, Target };
// Paints an empty socket into a canvas the size of one cell (the socket sits where a bubble's round part goes).
void PaintSocket(Canvas& canvas, const BubbleMetrics& metrics, ShortcutKind kind, SocketLook look, float scale);

// An app bubble's app, as the taskbar shows it: not running, running, or in front.
enum class BubbleState { Closed, Running, Active };
// Is a point (cell-local) on the bubble or its name?
bool BubbleHit(const BubbleMetrics& metrics, POINT local);
// Paints a bubble into a canvas the size of one cell.
void PaintBubble(Canvas& canvas, const BubbleMetrics& metrics, const IconImage* icon, const std::wstring& name,
                 ShortcutKind kind, bool hover, float scale, BubbleState state = BubbleState::Closed);
// A name pill for margins too small to show names under the bubbles (shown on hover).
void PaintBubbleLabel(Canvas& canvas, const std::wstring& name, float scale);
SIZE BubbleLabelSize(const std::wstring& name, float scale);
