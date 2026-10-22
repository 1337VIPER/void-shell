#pragma once

#include <string>
#include <vector>

#include "common.h"

// The global wallpaper's file (a virtual desktop can override it with its own).
std::wstring GlobalWallpaperPath();

// Loads an image scaled and cropped to cover width x height, like Windows' "Fill" fit, as top-down BGRA rows.
// Needs COM initialised on the calling thread.
bool LoadImageFill(const std::wstring& path, UINT width, UINT height, std::vector<uint8_t>& bgra);

void FillSolid(std::vector<uint8_t>& bgra, UINT width, UINT height, COLORREF color);
