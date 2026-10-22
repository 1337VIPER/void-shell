#include "settings.h"

#include <algorithm>
#include <cwctype>
#include <vector>

#include "desktops.h"
#include "zones.h"

namespace {

constexpr wchar_t kSection[] = L"VoidShell";
constexpr wchar_t kLayoutsSection[] = L"DesktopLayouts";
constexpr wchar_t kDockSection[] = L"Dock";
constexpr wchar_t kShortcutsSection[] = L"Shortcuts";

// "app|1|0.2500|0.5000|\\.\DISPLAY1|C:\Tools\app.exe|Name": the name comes last because it may contain anything.
std::wstring SaveShortcut(const Shortcut& s) {
    wchar_t numbers[64];
    swprintf_s(numbers, L"|%d|%.4f|%.4f|", s.band, s.fx, s.fy);
    return std::wstring(s.kind == ShortcutKind::App ? L"app" : L"folder") + numbers + s.monitor + L"|" + s.target + L"|" +
           s.name;
}

bool LoadShortcut(const std::wstring& text, Shortcut& s) {
    std::vector<std::wstring> fields;
    size_t start = 0;
    while (fields.size() < 6) {
        const size_t bar = text.find(L'|', start);
        if (bar == std::wstring::npos) return false;
        fields.push_back(text.substr(start, bar - start));
        start = bar + 1;
    }
    s.name = text.substr(start);
    if (fields[0] != L"app" && fields[0] != L"folder") return false;
    s.kind = fields[0] == L"app" ? ShortcutKind::App : ShortcutKind::Folder;
    s.band = std::clamp(_wtoi(fields[1].c_str()), 0, 3);
    s.fx = std::clamp(float(_wtof(fields[2].c_str())), 0.0f, 1.0f);
    s.fy = std::clamp(float(_wtof(fields[3].c_str())), 0.0f, 1.0f);
    s.monitor = fields[4];
    s.target = fields[5];
    if (!BandAllows(s.kind, s.band)) s.band = s.kind == ShortcutKind::App ? 1 : 0;
    return !s.target.empty();
}

bool ReadBool(const wchar_t* key, bool fallback, const std::wstring& path) {
    return GetPrivateProfileIntW(kSection, key, fallback ? 1 : 0, path.c_str()) != 0;
}

void WriteValue(const wchar_t* key, UINT value, const std::wstring& path) {
    WritePrivateProfileStringW(kSection, key, std::to_wstring(value).c_str(), path.c_str());
}

int ClampLayout(int layout) { return std::clamp(layout, 0, int(ZoneLayouts().size()) - 1); }

std::wstring KeyName(UINT key) {
    UINT scan = MapVirtualKeyW(key, MAPVK_VK_TO_VSC);
    switch (key) {
        // These share scan codes with the number pad; the extended bit picks the right name.
        case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN: case VK_PRIOR: case VK_NEXT:
        case VK_END: case VK_HOME: case VK_INSERT: case VK_DELETE: case VK_DIVIDE:
            scan |= 0x100;
            break;
    }
    wchar_t name[64] = {};
    if (scan && GetKeyNameTextW(LONG(scan << 16), name, ARRAYSIZE(name)) > 0) {
        std::wstring text = name;
        // Some layouts report names in capitals ("SPACE"); show them as words.
        if (text.size() > 1) std::transform(text.begin() + 1, text.end(), text.begin() + 1, ::towlower);
        return text;
    }
    return L"Key " + std::to_wstring(key);
}

}  // namespace

int Settings::LayoutFor(const GUID& desktop) const {
    const auto found = desktopLayouts.find(GuidToString(desktop));
    return ClampLayout(found != desktopLayouts.end() ? found->second : defaultLayout);
}

void Settings::SetLayoutFor(const GUID& desktop, int layout) {
    desktopLayouts[GuidToString(desktop)] = ClampLayout(layout);
}

void Settings::Load(const std::wstring& path) {
    hotkeyModifiers = GetPrivateProfileIntW(kSection, L"HotkeyModifiers", hotkeyModifiers, path.c_str());
    hotkeyKey = GetPrivateProfileIntW(kSection, L"HotkeyKey", hotkeyKey, path.c_str());
    pauseInFullscreen = ReadBool(L"PauseInFullscreen", pauseInFullscreen, path);
    frame = ReadBool(L"ShellFrame", frame, path);
    frameAllMonitors = ReadBool(L"FrameOnAllMonitors", frameAllMonitors, path);
    frameWide = ReadBool(L"WideMargin", frameWide, path);
    frameMargin = std::clamp(int(GetPrivateProfileIntW(kSection, L"MarginSize", frameMargin, path.c_str())), 0, 2);
    matrixRain = ReadBool(L"MatrixRain", matrixRain, path);
    rainColor = std::clamp(int(GetPrivateProfileIntW(kSection, L"RainColour", rainColor, path.c_str())), 0, 2);
    systemStats = ReadBool(L"SystemStats", systemStats, path);
    showSockets = ReadBool(L"ShowSockets", showSockets, path);
    wchar_t brand[64] = {};
    GetPrivateProfileStringW(kSection, L"BrandName", brandName.c_str(), brand, ARRAYSIZE(brand), path.c_str());
    brandName = brand;
    dock = ReadBool(L"Dock", dock, path);
    dockAllDesktops = ReadBool(L"DockAllDesktops", dockAllDesktops, path);
    hideWindowsTaskbar = ReadBool(L"HideWindowsTaskbar", hideWindowsTaskbar, path);
    taskbarRestore = std::clamp(int(GetPrivateProfileIntW(kSection, L"TaskbarRestore", taskbarRestore, path.c_str())), -1, 1);
    dockSeeded = GetPrivateProfileIntW(kDockSection, L"Seeded", 0, path.c_str()) != 0;
    dockPins.clear();
    for (int i = 0;; ++i) {
        wchar_t pin[MAX_PATH * 2] = {};
        const std::wstring key = L"Pin" + std::to_wstring(i);
        if (!GetPrivateProfileStringW(kDockSection, key.c_str(), L"", pin, ARRAYSIZE(pin), path.c_str())) break;
        dockPins.push_back(pin);
    }
    shortcuts.clear();
    for (int i = 0;; ++i) {
        std::vector<wchar_t> value(4096);
        const std::wstring key = L"S" + std::to_wstring(i);
        if (!GetPrivateProfileStringW(kShortcutsSection, key.c_str(), L"", value.data(), DWORD(value.size()), path.c_str()))
            break;
        Shortcut shortcut;
        if (LoadShortcut(value.data(), shortcut)) shortcuts.push_back(shortcut);
    }
    snap = ReadBool(L"SnapToZones", snap, path);
    autoPlace = ReadBool(L"PlaceNewWindows", autoPlace, path);
    // Layouts are stored by index; files from before "One window" was added first count from one lower.
    const int version = int(GetPrivateProfileIntW(kSection, L"LayoutVersion", 1, path.c_str()));
    const auto stored = [version](int index) { return ClampLayout(version < kLayoutVersion ? index + 1 : index); };
    // Phase 3 kept a single layout under ZoneLayout; it becomes the default.
    int saved = int(GetPrivateProfileIntW(kSection, L"DefaultLayout", UINT(-1), path.c_str()));
    if (saved < 0) saved = int(GetPrivateProfileIntW(kSection, L"ZoneLayout", UINT(-1), path.c_str()));
    if (saved >= 0) defaultLayout = stored(saved);
    startAtLogin = ReadBool(L"StartAtLogin", startAtLogin, path);
    runAsAdmin = ReadBool(L"RunAsAdministrator", runAsAdmin, path);

    desktopLayouts.clear();
    std::vector<wchar_t> buffer(32768);
    const DWORD length = GetPrivateProfileSectionW(kLayoutsSection, buffer.data(), DWORD(buffer.size()), path.c_str());
    // The section comes back as "key=value\0key=value\0\0".
    for (const wchar_t* entry = buffer.data(); entry < buffer.data() + length && *entry; entry += wcslen(entry) + 1) {
        const std::wstring line = entry;
        const size_t equals = line.find(L'=');
        if (equals != std::wstring::npos) desktopLayouts[line.substr(0, equals)] = stored(_wtoi(line.c_str() + equals + 1));
    }
}

void Settings::Save(const std::wstring& path) const {
    WriteValue(L"HotkeyModifiers", hotkeyModifiers, path);
    WriteValue(L"HotkeyKey", hotkeyKey, path);
    WriteValue(L"PauseInFullscreen", pauseInFullscreen, path);
    WriteValue(L"ShellFrame", frame, path);
    WriteValue(L"FrameOnAllMonitors", frameAllMonitors, path);
    WriteValue(L"WideMargin", frameWide, path);
    WriteValue(L"MarginSize", UINT(frameMargin), path);
    WriteValue(L"MatrixRain", matrixRain, path);
    WriteValue(L"RainColour", UINT(rainColor), path);
    WriteValue(L"SystemStats", systemStats, path);
    WriteValue(L"ShowSockets", showSockets, path);
    WritePrivateProfileStringW(kSection, L"BrandName", brandName.c_str(), path.c_str());
    WriteValue(L"Dock", dock, path);
    WriteValue(L"DockAllDesktops", dockAllDesktops, path);
    WriteValue(L"HideWindowsTaskbar", hideWindowsTaskbar, path);
    WritePrivateProfileStringW(kSection, L"TaskbarRestore", std::to_wstring(taskbarRestore).c_str(), path.c_str());
    WritePrivateProfileStringW(kDockSection, nullptr, nullptr, path.c_str());  // clear, then rewrite
    WritePrivateProfileStringW(kDockSection, L"Seeded", dockSeeded ? L"1" : L"0", path.c_str());
    for (size_t i = 0; i < dockPins.size(); ++i)
        WritePrivateProfileStringW(kDockSection, (L"Pin" + std::to_wstring(i)).c_str(), dockPins[i].c_str(), path.c_str());
    WritePrivateProfileStringW(kShortcutsSection, nullptr, nullptr, path.c_str());  // clear, then rewrite
    for (size_t i = 0; i < shortcuts.size(); ++i)
        WritePrivateProfileStringW(kShortcutsSection, (L"S" + std::to_wstring(i)).c_str(), SaveShortcut(shortcuts[i]).c_str(),
                                   path.c_str());
    WriteValue(L"SnapToZones", snap, path);
    WriteValue(L"PlaceNewWindows", autoPlace, path);
    WriteValue(L"DefaultLayout", UINT(defaultLayout), path);
    WriteValue(L"LayoutVersion", UINT(kLayoutVersion), path);
    WritePrivateProfileStringW(kSection, L"ZoneLayout", nullptr, path.c_str());  // replaced by DefaultLayout
    WriteValue(L"StartAtLogin", startAtLogin, path);
    WriteValue(L"RunAsAdministrator", runAsAdmin, path);

    WritePrivateProfileStringW(kLayoutsSection, nullptr, nullptr, path.c_str());  // clear, then rewrite
    for (const auto& [desktop, layout] : desktopLayouts)
        WritePrivateProfileStringW(kLayoutsSection, desktop.c_str(), std::to_wstring(layout).c_str(), path.c_str());
}

std::wstring HotkeyText(UINT modifiers, UINT key) {
    std::wstring text;
    const auto add = [&](const wchar_t* part) {
        if (!text.empty()) text += L" + ";
        text += part;
    };
    if (modifiers & MOD_CONTROL) add(L"Ctrl");
    if (modifiers & MOD_ALT) add(L"Alt");
    if (modifiers & MOD_SHIFT) add(L"Shift");
    if (modifiers & MOD_WIN) add(L"Win");
    add(KeyName(key).c_str());
    return text;
}
