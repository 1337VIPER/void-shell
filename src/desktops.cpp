#include "desktops.h"

#include <servprov.h>
#include <winstring.h>

#include <cstdlib>
#include <cstring>

namespace {

constexpr wchar_t kVirtualDesktopsKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\VirtualDesktops";

std::wstring FromHString(HSTRING value) {
    UINT32 length = 0;
    const wchar_t* buffer = WindowsGetStringRawBuffer(value, &length);
    std::wstring text(buffer ? buffer : L"", length);
    WindowsDeleteString(value);
    return text;
}

bool ReadBinary(const std::wstring& key, const wchar_t* value, std::vector<uint8_t>& data) {
    DWORD size = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, key.c_str(), value, RRF_RT_REG_BINARY, nullptr, nullptr, &size) !=
            ERROR_SUCCESS ||
        size == 0) {
        return false;
    }
    data.resize(size);
    return RegGetValueW(HKEY_CURRENT_USER, key.c_str(), value, RRF_RT_REG_BINARY, nullptr, data.data(), &size) ==
           ERROR_SUCCESS;
}

std::wstring ReadString(const std::wstring& key, const wchar_t* value) {
    wchar_t buffer[1024] = {};
    DWORD size = sizeof(buffer);
    if (RegGetValueW(HKEY_CURRENT_USER, key.c_str(), value, RRF_RT_REG_SZ, nullptr, buffer, &size) != ERROR_SUCCESS) {
        return L"";
    }
    return buffer;
}

int IndexOf(const std::vector<DesktopInfo>& desktops, const GUID& id) {
    for (size_t i = 0; i < desktops.size(); ++i)
        if (desktops[i].id == id) return int(i);
    return -1;
}

INPUT Key(WORD vk, bool up) {
    INPUT input = {};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = vk;
    input.ki.dwFlags = (up ? KEYEVENTF_KEYUP : 0u) | ((vk == VK_LEFT || vk == VK_RIGHT) ? KEYEVENTF_EXTENDEDKEY : 0u);
    return input;
}

// Presses Win+Ctrl+<key>: Windows' own virtual desktop shortcuts.
void PressDesktopShortcut(WORD key) {
    INPUT inputs[] = {Key(VK_LWIN, false), Key(VK_CONTROL, false), Key(key, false),
                      Key(key, true),      Key(VK_CONTROL, true),  Key(VK_LWIN, true)};
    SendInput(ARRAYSIZE(inputs), inputs, sizeof(INPUT));
}

}  // namespace

std::wstring GuidToString(const GUID& id) {
    wchar_t text[64] = {};
    StringFromGUID2(id, text, ARRAYSIZE(text));
    return text;
}

void VirtualDesktops::Init() {
    manager_.Reset();
    ComPtr<IServiceProvider> shell;
    HRESULT hr =
        CoCreateInstance(__uuidof(vdcom::ImmersiveShell), nullptr, CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(&shell));
    if (SUCCEEDED(hr)) {
        hr = shell->QueryService(__uuidof(vdcom::VirtualDesktopManagerInternalService), IID_PPV_ARGS(&manager_));
    }
    // Sanity check before trusting the layout: the count and the list must agree.
    if (SUCCEEDED(hr)) {
        UINT count = 0;
        UINT listed = 0;
        ComPtr<IObjectArray> desktops;
        hr = manager_->GetCount(&count);
        if (SUCCEEDED(hr)) hr = manager_->GetDesktops(&desktops);
        if (SUCCEEDED(hr)) hr = desktops->GetCount(&listed);
        if (SUCCEEDED(hr) && (count == 0 || count != listed)) hr = E_UNEXPECTED;
    }
    if (FAILED(hr)) {
        Log("Shell virtual desktop API unavailable (0x%08X); using the registry and keyboard shortcuts", unsigned(hr));
        manager_.Reset();
    } else {
        Log("Using the shell virtual desktop API");
    }

    if (FAILED(CoCreateInstance(__uuidof(vdcom::PublicVirtualDesktopManager), nullptr, CLSCTX_ALL,
                                IID_PPV_ARGS(&publicManager_)))) {
        publicManager_.Reset();
    }
}

bool VirtualDesktops::List(std::vector<DesktopInfo>& desktops, GUID& current) {
    if (manager_) {
        if (ListWithCom(desktops, current)) return true;
        // Explorer may have restarted, which drops the connection: reconnect once.
        Init();
        if (manager_ && ListWithCom(desktops, current)) return true;
    }
    return ListFromRegistry(desktops, current);
}

bool VirtualDesktops::ListWithCom(std::vector<DesktopInfo>& desktops, GUID& current) {
    desktops.clear();
    ComPtr<IObjectArray> array;
    CHECK_HR(manager_->GetDesktops(&array));
    UINT count = 0;
    CHECK_HR(array->GetCount(&count));
    for (UINT i = 0; i < count; ++i) {
        ComPtr<vdcom::IVirtualDesktop> desktop;
        CHECK_HR(array->GetAt(i, IID_PPV_ARGS(&desktop)));
        DesktopInfo info;
        CHECK_HR(desktop->GetId(&info.id));
        HSTRING text = nullptr;
        if (SUCCEEDED(desktop->GetName(&text))) info.name = FromHString(text);
        text = nullptr;
        if (SUCCEEDED(desktop->GetWallpaperPath(&text))) info.wallpaper = FromHString(text);
        desktops.push_back(std::move(info));
    }
    ComPtr<vdcom::IVirtualDesktop> active;
    CHECK_HR(manager_->GetCurrentDesktop(&active));
    CHECK_HR(active->GetId(&current));
    return !desktops.empty();
}

bool VirtualDesktops::Current(GUID& current) {
    if (manager_) {
        ComPtr<vdcom::IVirtualDesktop> active;
        if (SUCCEEDED(manager_->GetCurrentDesktop(&active)) && SUCCEEDED(active->GetId(&current))) return true;
    }
    std::vector<DesktopInfo> desktops;
    return ListFromRegistry(desktops, current);
}

bool VirtualDesktops::Create(GUID& created) {
    if (manager_) {
        ComPtr<vdcom::IVirtualDesktop> desktop;
        CHECK_HR(manager_->CreateDesktop(&desktop));
        CHECK_HR(desktop->GetId(&created));
        return true;
    }

    std::vector<DesktopInfo> before;
    GUID current = {};
    ListFromRegistry(before, current);
    PressDesktopShortcut('D');
    // The shortcut creates the desktop and switches to it; wait for Explorer to record it.
    for (int attempt = 0; attempt < 32; ++attempt) {
        Sleep(25);
        std::vector<DesktopInfo> after;
        if (!ListFromRegistry(after, current) || after.size() <= before.size()) continue;
        for (const DesktopInfo& desktop : after) {
            if (IndexOf(before, desktop.id) < 0) {
                created = desktop.id;
                return true;
            }
        }
    }
    Log("Win+Ctrl+D did not produce a new desktop");
    return false;
}

bool VirtualDesktops::Switch(const GUID& id) {
    if (manager_) {
        ComPtr<vdcom::IVirtualDesktop> desktop;
        CHECK_HR(manager_->FindDesktop(&id, &desktop));
        CHECK_HR(manager_->SwitchDesktop(desktop.Get()));
        return true;
    }

    std::vector<DesktopInfo> desktops;
    GUID current = {};
    if (!ListFromRegistry(desktops, current)) return false;
    const int from = IndexOf(desktops, current);
    const int to = IndexOf(desktops, id);
    if (from < 0 || to < 0) return false;
    for (int step = 0; step < std::abs(to - from); ++step) PressDesktopShortcut(to > from ? VK_RIGHT : VK_LEFT);
    return true;
}

bool VirtualDesktops::Move(const GUID& id, int index) {
    if (!manager_) return false;
    ComPtr<vdcom::IVirtualDesktop> desktop;
    CHECK_HR(manager_->FindDesktop(&id, &desktop));
    CHECK_HR(manager_->MoveDesktop(desktop.Get(), UINT(index)));
    return true;
}

bool VirtualDesktops::Remove(const GUID& id, const GUID& fallback) {
    if (!manager_) return false;
    ComPtr<vdcom::IVirtualDesktop> desktop;
    ComPtr<vdcom::IVirtualDesktop> destination;
    CHECK_HR(manager_->FindDesktop(&id, &desktop));
    CHECK_HR(manager_->FindDesktop(&fallback, &destination));
    CHECK_HR(manager_->RemoveDesktop(desktop.Get(), destination.Get()));
    return true;
}

bool VirtualDesktops::MoveWindowTo(HWND window, const GUID& id) {
    if (!publicManager_) return false;
    CHECK_HR(publicManager_->MoveWindowToDesktop(window, id));
    return true;
}

bool VirtualDesktops::WindowDesktop(HWND window, GUID& id) {
    return publicManager_ && SUCCEEDED(publicManager_->GetWindowDesktopId(window, &id));
}

bool VirtualDesktops::ListFromRegistry(std::vector<DesktopInfo>& desktops, GUID& current) {
    desktops.clear();
    current = GUID{};
    std::vector<uint8_t> data;
    bool haveCurrent = ReadBinary(kVirtualDesktopsKey, L"CurrentVirtualDesktop", data) && data.size() >= sizeof(GUID);
    if (!haveCurrent) {
        // Windows 10 keeps the current desktop per logon session.
        DWORD session = 0;
        ProcessIdToSessionId(GetCurrentProcessId(), &session);
        const std::wstring key = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\SessionInfo\\" +
                                 std::to_wstring(session) + L"\\VirtualDesktops";
        haveCurrent = ReadBinary(key, L"CurrentVirtualDesktop", data) && data.size() >= sizeof(GUID);
    }
    if (haveCurrent) memcpy(&current, data.data(), sizeof(GUID));

    if (ReadBinary(kVirtualDesktopsKey, L"VirtualDesktopIDs", data)) {
        for (size_t offset = 0; offset + sizeof(GUID) <= data.size(); offset += sizeof(GUID)) {
            DesktopInfo info;
            memcpy(&info.id, data.data() + offset, sizeof(GUID));
            const std::wstring key = std::wstring(kVirtualDesktopsKey) + L"\\Desktops\\" + GuidToString(info.id);
            info.name = ReadString(key, L"Name");
            info.wallpaper = ReadString(key, L"Wallpaper");
            desktops.push_back(std::move(info));
        }
    }
    // With only ever one desktop, Windows may not have written the list yet.
    if (desktops.empty() && haveCurrent) {
        DesktopInfo only;
        only.id = current;
        desktops.push_back(only);
    }
    return !desktops.empty();
}
