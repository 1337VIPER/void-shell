#pragma once

#include "canvas.h"
#include "common.h"

// The Venom look, taken from Venom Board: its dark-theme design tokens (index.html) and its mark (assets/icon.svg).
namespace venom {

constexpr Rgba kVoid = {0.024f, 0.024f, 0.035f, 1.0f};        // --void   #060609
constexpr Rgba kHide = {0.055f, 0.055f, 0.082f, 1.0f};        // --hide   #0e0e15
constexpr Rgba kCard = {0.063f, 0.063f, 0.090f, 1.0f};        // --card   #101017
constexpr Rgba kCardHi = {0.110f, 0.110f, 0.157f, 1.0f};      // --card-hi #1c1c28
constexpr Rgba kBone = {0.945f, 0.933f, 0.949f, 1.0f};        // --bone   #f1eef2
constexpr Rgba kBoneDim = {0.616f, 0.600f, 0.667f, 1.0f};     // --bone-dim #9d99aa
constexpr Rgba kBoneFaint = {0.373f, 0.361f, 0.424f, 1.0f};   // --bone-faint #5f5c6c
constexpr Rgba kFang = {1.0f, 0.165f, 0.310f, 1.0f};          // --fang   #ff2a4f (crimson)
constexpr Rgba kSheen = {0.255f, 0.341f, 1.0f, 1.0f};         // --sheen  #4157ff (electric blue)
constexpr Rgba kOk = {0.239f, 1.0f, 0.545f, 1.0f};            // --ok     #3dff8b
constexpr Rgba kRun = {0.184f, 0.839f, 1.0f, 1.0f};           // --run    #2fd6ff
constexpr Rgba kMarkFill = {0.020f, 0.020f, 0.027f, 1.0f};    // mark body #050507
constexpr Rgba kMarkStroke = {0.165f, 0.165f, 0.220f, 1.0f};  // mark edge #2a2a38

constexpr Rgba WithAlpha(Rgba color, float alpha) { return {color.r, color.g, color.b, alpha}; }

// Venom Board's fonts (Unbounded, Manrope, JetBrains Mono) when installed, otherwise the closest Windows fonts.
const wchar_t* DisplayFont();
const wchar_t* UiFont();
const wchar_t* MonoFont();
const wchar_t* RainFont();  // has half-width katakana for the code rain
const wchar_t* IconFont();  // Windows' own symbol font (search, volume, close), as the taskbar uses

// Plain: exactly as icon.svg. Glow: the crimson-outlined version from the social-kit avatar, readable on dark.
enum class MarkStyle { Plain, Glow };

// Draws the Venom mark centred at (cx, cy), `size` pixels tall. `lit` brightens the glow (hover).
void DrawMark(Canvas& canvas, float cx, float cy, float size, MarkStyle style, bool lit = false);

// The glowing mark as a Windows icon (tray, taskbar, window icons).
HICON CreateMarkIcon(int size);

}  // namespace venom
