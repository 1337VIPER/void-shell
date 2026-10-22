#pragma once

#include <string>
#include <vector>

#include "common.h"

// A square image in premultiplied BGRA, ready to composite onto a Canvas.
struct IconImage {
    int size = 0;
    std::vector<uint32_t> pixels;
    bool Empty() const { return pixels.empty(); }
};

// The shell's icon for a file, shortcut or packaged app ("shell:AppsFolder\<AppUserModelID>"), at a pixel size.
// This is the same high-resolution icon Explorer and the taskbar show. Needs COM.
bool ShellIcon(const std::wstring& parsingName, int size, IconImage& out);
// A window's own icon (WM_GETICON, then its class icon), for apps the shell can't give one for.
bool WindowIcon(HWND window, int size, IconImage& out);
// A friendly name for an exe: its FileDescription ("Google Chrome"), else its file name without extension.
std::wstring AppNameFromExe(const std::wstring& path);
