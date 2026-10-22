#pragma once

#include <initializer_list>
#include <string>
#include <vector>

#include "common.h"

// Windows' own taskbar: its auto-hide state, and the shortcuts pinned to it.
bool TaskbarAutoHide();
void SetTaskbarAutoHide(bool on);
// Full paths of the .lnk files pinned to the Windows taskbar (read-only; used to seed the dock).
std::vector<std::wstring> TaskbarPinnedShortcuts();

// What a shortcut points at. Needs COM.
struct ShortcutInfo {
    std::wstring target;     // the exe or document, environment variables expanded
    std::wstring arguments;
    std::wstring appId;      // its AppUserModelID, if the shortcut carries one
};
bool ReadShortcut(const std::wstring& path, ShortcutInfo& info);

// Presses a key combination, e.g. {VK_LWIN, 'S'}. Used for Start, search, notifications and the Win+X menu.
void PressKeys(std::initializer_list<WORD> keys);
