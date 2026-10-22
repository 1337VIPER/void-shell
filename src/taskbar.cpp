#include "taskbar.h"

#include <initguid.h>  // defines PKEY_AppUserModel_ID for this program

#include <propkey.h>
#include <propvarutil.h>
#include <shellapi.h>
#include <knownfolders.h>
#include <shlobj.h>

#include <algorithm>

namespace {

HWND TaskbarWindow() { return FindWindowW(L"Shell_TrayWnd", nullptr); }

}  // namespace

bool TaskbarAutoHide() {
    APPBARDATA data = {sizeof(data)};
    data.hWnd = TaskbarWindow();
    return (SHAppBarMessage(ABM_GETSTATE, &data) & ABS_AUTOHIDE) != 0;
}

void SetTaskbarAutoHide(bool on) {
    APPBARDATA data = {sizeof(data)};
    data.hWnd = TaskbarWindow();
    if (!data.hWnd) return;
    data.lParam = on ? ABS_AUTOHIDE : ABS_ALWAYSONTOP;
    SHAppBarMessage(ABM_SETSTATE, &data);
    Log("Windows taskbar auto-hide %s", on ? "on" : "off");
}

std::vector<std::wstring> TaskbarPinnedShortcuts() {
    std::vector<std::wstring> pins;
    PWSTR roaming = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roaming))) return pins;
    const std::wstring folder = std::wstring(roaming) + L"\\Microsoft\\Internet Explorer\\Quick Launch\\User Pinned\\TaskBar\\";
    CoTaskMemFree(roaming);
    WIN32_FIND_DATAW found = {};
    HANDLE search = FindFirstFileW((folder + L"*.lnk").c_str(), &found);
    if (search == INVALID_HANDLE_VALUE) return pins;
    do {
        pins.push_back(folder + found.cFileName);
    } while (FindNextFileW(search, &found));
    FindClose(search);
    std::sort(pins.begin(), pins.end());
    return pins;
}

bool ReadShortcut(const std::wstring& path, ShortcutInfo& info) {
    info = ShortcutInfo();
    ComPtr<IShellLinkW> link;
    CHECK_HR(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)));
    ComPtr<IPersistFile> file;
    CHECK_HR(link.As(&file));
    CHECK_HR(file->Load(path.c_str(), STGM_READ));
    wchar_t target[MAX_PATH] = {};
    if (SUCCEEDED(link->GetPath(target, MAX_PATH, nullptr, SLGP_RAWPATH))) {
        wchar_t expanded[MAX_PATH] = {};
        ExpandEnvironmentStringsW(target, expanded, MAX_PATH);
        info.target = expanded;
    }
    wchar_t arguments[1024] = {};
    if (SUCCEEDED(link->GetArguments(arguments, ARRAYSIZE(arguments)))) info.arguments = arguments;
    ComPtr<IPropertyStore> store;
    if (SUCCEEDED(link.As(&store))) {
        PROPVARIANT value;
        PropVariantInit(&value);
        if (SUCCEEDED(store->GetValue(PKEY_AppUserModel_ID, &value)) && value.vt == VT_LPWSTR && value.pwszVal)
            info.appId = value.pwszVal;
        PropVariantClear(&value);
    }
    return true;
}

void PressKeys(std::initializer_list<WORD> keys) {
    std::vector<INPUT> inputs;
    for (const WORD key : keys) {
        INPUT input = {};
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = key;
        inputs.push_back(input);
    }
    for (auto it = keys.end(); it != keys.begin();) {
        INPUT input = {};
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = *--it;
        input.ki.dwFlags = KEYEVENTF_KEYUP;
        inputs.push_back(input);
    }
    SendInput(UINT(inputs.size()), inputs.data(), sizeof(INPUT));
}
