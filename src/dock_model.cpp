#include "dock.h"

#include <appmodel.h>
#include <dwmapi.h>
#include <propkey.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <algorithm>

#include "taskbar.h"

namespace {

constexpr wchar_t kAppsFolder[] = L"shell:AppsFolder\\";

std::wstring Lower(std::wstring text) {
    if (!text.empty()) CharLowerBuffW(text.data(), DWORD(text.size()));
    return text;
}

bool StartsWith(const std::wstring& text, const wchar_t* prefix) {
    const size_t length = wcslen(prefix);
    return text.size() >= length && _wcsnicmp(text.c_str(), prefix, length) == 0;
}

// Which app a window belongs to, as the taskbar sees it: the window's own AppUserModelID, else its packaged app's
// (Store-style apps carry it on the process), and the exe it runs from.
void WindowIdentity(HWND window, std::wstring& appId, std::wstring& exe) {
    appId.clear();
    exe.clear();
    ComPtr<IPropertyStore> store;
    if (SUCCEEDED(SHGetPropertyStoreForWindow(window, IID_PPV_ARGS(&store)))) {
        PROPVARIANT value;
        PropVariantInit(&value);
        if (SUCCEEDED(store->GetValue(PKEY_AppUserModel_ID, &value)) && value.vt == VT_LPWSTR && value.pwszVal)
            appId = value.pwszVal;
        PropVariantClear(&value);
    }
    DWORD process = 0;
    GetWindowThreadProcessId(window, &process);
    HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process);
    if (handle) {
        wchar_t path[MAX_PATH] = {};
        DWORD size = MAX_PATH;
        if (QueryFullProcessImageNameW(handle, 0, path, &size)) exe = Lower(path);
        if (appId.empty()) {
            wchar_t packaged[APPLICATION_USER_MODEL_ID_MAX_LENGTH] = {};
            UINT32 length = ARRAYSIZE(packaged);
            if (GetApplicationUserModelId(handle, &length, packaged) == ERROR_SUCCESS) appId = packaged;
        }
        CloseHandle(handle);
    }
    if (appId.empty()) {
        // Explorer's folder windows group under File Explorer.
        wchar_t className[32] = {};
        GetClassNameW(window, className, ARRAYSIZE(className));
        if (wcscmp(className, L"CabinetWClass") == 0) appId = L"Microsoft.Windows.Explorer";
    }
}

std::wstring ShellDisplayName(const std::wstring& parsingName) {
    ComPtr<IShellItem> item;
    if (FAILED(SHCreateItemFromParsingName(parsingName.c_str(), nullptr, IID_PPV_ARGS(&item)))) return L"";
    PWSTR name = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_NORMALDISPLAY, &name)) || !name) return L"";
    std::wstring text = name;
    CoTaskMemFree(name);
    return text;
}

std::wstring FileName(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

std::wstring FileStem(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    std::wstring name = slash == std::wstring::npos ? path : path.substr(slash + 1);
    const size_t dot = name.find_last_of(L'.');
    return dot == std::wstring::npos ? name : name.substr(0, dot);
}

}  // namespace

std::wstring ExeFromAppsFolderId(const std::wstring& id) {
    if (id.size() > 3 && id[1] == L':' && id[2] == L'\\') return id;  // already a path
    const size_t close = id.find(L"}\\");
    if (id.empty() || id[0] != L'{' || close == std::wstring::npos) return L"";
    GUID folder = {};
    if (FAILED(CLSIDFromString(id.substr(0, close + 1).c_str(), &folder))) return L"";
    PWSTR base = nullptr;
    if (FAILED(SHGetKnownFolderPath(folder, KF_FLAG_DEFAULT, nullptr, &base)) || !base) return L"";
    const std::wstring path = std::wstring(base) + id.substr(close + 1);
    CoTaskMemFree(base);
    return path;
}

bool DockModel::IsTaskbarWindow(HWND window) {
    if (!IsWindowVisible(window)) return false;
    const LONG exStyle = GetWindowLongW(window, GWL_EXSTYLE);
    if (!(exStyle & WS_EX_APPWINDOW)) {
        if (GetWindow(window, GW_OWNER) || (exStyle & (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE))) return false;
    }
    DWORD cloaked = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && (cloaked & DWM_CLOAKED_APP))
        return false;  // e.g. a suspended store app's frame
    if (GetWindowTextLengthW(window) == 0) return false;
    wchar_t className[64] = {};
    GetClassNameW(window, className, ARRAYSIZE(className));
    for (const wchar_t* shell : {L"Progman", L"WorkerW", L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd"})
        if (wcscmp(className, shell) == 0) return false;
    return true;
}

DockModel::PinEntry DockModel::ReadPin(const std::wstring& path) {
    PinEntry pin;
    pin.path = path;
    if (StartsWith(path, kAppsFolder)) {
        // Start names an app by its app ID, or (for a desktop app without one) by its exe's path.
        const std::wstring id = path.substr(wcslen(kAppsFolder));
        const std::wstring exe = ExeFromAppsFolderId(id);
        if (exe.empty()) {
            pin.appId = id;
        } else {
            pin.exe = Lower(exe);
        }
        pin.name = ShellDisplayName(path);
    } else if (path.size() > 4 && _wcsicmp(path.c_str() + path.size() - 4, L".lnk") == 0) {
        ShortcutInfo info;
        if (ReadShortcut(path, info)) {
            pin.appId = info.appId;
            pin.exe = Lower(info.target);
            // A shortcut can start a packaged app through Explorer ("explorer.exe shell:AppsFolder\<id>"): it is
            // that app, not Explorer.
            const std::wstring arguments = Lower(info.arguments);
            const size_t at = arguments.find(L"shell:appsfolder\\");
            if (at != std::wstring::npos) {
                std::wstring id = info.arguments.substr(at + wcslen(kAppsFolder));
                id = id.substr(0, id.find_first_of(L" \""));
                if (pin.appId.empty()) pin.appId = id;
            }
            if (FileName(pin.exe) == L"explorer.exe" && !info.arguments.empty()) pin.exe.clear();
        }
        pin.name = FileStem(path);
    } else {
        pin.exe = Lower(path);
        pin.name = AppNameFromExe(path);
    }
    if (pin.name.empty()) pin.name = FileStem(path);
    return pin;
}

void DockModel::SetPins(const std::vector<std::wstring>& paths) {
    pins_.clear();
    for (const std::wstring& path : paths) pins_.push_back(ReadPin(path));
}

std::vector<std::wstring> DockModel::PinPaths() const {
    std::vector<std::wstring> paths;
    for (const PinEntry& pin : pins_) paths.push_back(pin.path);
    return paths;
}

bool DockModel::Pin(const DockApp& app) {
    if (app.launch.empty() || app.pinned) return false;
    PinEntry pin = ReadPin(app.launch);
    if (pin.appId.empty()) pin.appId = app.appId;
    if (pin.exe.empty()) pin.exe = app.exe;
    pin.name = app.name;
    pins_.push_back(pin);
    return true;
}

void DockModel::Unpin(const DockApp& app) {
    pins_.erase(std::remove_if(pins_.begin(), pins_.end(),
                               [&](const PinEntry& pin) { return pin.path == app.launch; }),
                pins_.end());
}

const DockModel::Known& DockModel::Lookup(const std::wstring& appId, const std::wstring& exe) {
    const std::wstring key = appId.empty() ? exe : appId;
    auto found = known_.find(key);
    if (found != known_.end()) return found->second;
    Known known;
    if (!appId.empty()) {
        known.name = ShellDisplayName(kAppsFolder + appId);
        known.inAppsFolder = !known.name.empty();
    }
    if (known.name.empty() && !exe.empty()) {
        wchar_t self[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, self, MAX_PATH);
        known.name = Lower(self) == exe ? L"VenomMind" : AppNameFromExe(exe);
    }
    if (known.name.empty()) known.name = L"App";
    return known_[key] = known;
}

void DockModel::Refresh(const std::function<bool(HWND)>& include, HWND foreground, const std::set<HWND>& flashing) {
    std::vector<DockApp> apps;
    for (const PinEntry& pin : pins_) {
        DockApp app;
        app.appId = pin.appId;
        app.exe = pin.exe;
        app.name = pin.name;
        app.launch = pin.path;
        app.iconSource = pin.path;
        app.pinned = true;
        apps.push_back(std::move(app));
    }

    struct Found {
        HWND window;
        std::wstring appId, exe, title;
    };
    std::vector<Found> found;
    EnumWindows([](HWND window, LPARAM list) -> BOOL {
        if (IsTaskbarWindow(window)) reinterpret_cast<std::vector<Found>*>(list)->push_back({window, L"", L"", L""});
        return TRUE;
    }, reinterpret_cast<LPARAM>(&found));

    std::vector<DockApp> running;
    for (Found& f : found) {
        if (include && !include(f.window)) continue;
        WindowIdentity(f.window, f.appId, f.exe);
        wchar_t title[256] = {};
        GetWindowTextW(f.window, title, ARRAYSIZE(title));
        f.title = title;
        // Same app: same AppUserModelID, or the same exe when either side has no ID.
        const auto matches = [&](const DockApp& app) {
            if (!f.appId.empty() && !app.appId.empty()) return f.appId == app.appId;
            return !f.exe.empty() && f.exe == app.exe;
        };
        DockApp* app = nullptr;
        for (DockApp& candidate : apps) if (!app && matches(candidate)) app = &candidate;
        for (DockApp& candidate : running) if (!app && matches(candidate)) app = &candidate;
        // Apps that set their ID for the whole process (Electron, Squirrel installs) don't show it on their windows,
        // and their pinned shortcut often points at a launcher stub with the same file name. Match those by name.
        if (!app && f.appId.empty() && !f.exe.empty()) {
            const std::wstring file = FileName(f.exe);
            for (DockApp& candidate : apps)
                if (!app && !candidate.exe.empty() && FileName(candidate.exe) == file) app = &candidate;
        }
        if (!app) {
            const Known& known = Lookup(f.appId, f.exe);
            DockApp fresh;
            fresh.appId = f.appId;
            fresh.exe = f.exe;
            fresh.name = known.name;
            fresh.launch = known.inAppsFolder ? kAppsFolder + f.appId : f.exe;
            fresh.iconSource = fresh.launch;
            fresh.iconWindow = f.window;
            running.push_back(std::move(fresh));
            app = &running.back();
        }
        app->windows.push_back({f.window, f.title, int(app->windows.size())});
        if (!app->iconWindow) app->iconWindow = f.window;
        if (f.window == foreground) app->active = true;
        if (flashing.count(f.window)) app->attention = true;
    }

    // Windows in handle order, which roughly follows the order they were opened in and doesn't change as you
    // switch between them.
    for (std::vector<DockApp>* list : {&apps, &running})
        for (DockApp& app : *list)
            std::sort(app.windows.begin(), app.windows.end(), [](const DockWindow& a, const DockWindow& b) {
                return reinterpret_cast<uintptr_t>(a.window) < reinterpret_cast<uintptr_t>(b.window);
            });

    // Running apps keep the order they first appeared in.
    const auto keyOf = [](const DockApp& app) { return app.appId.empty() ? app.exe : app.appId; };
    std::vector<std::wstring> order;
    for (const std::wstring& key : runningOrder_) {
        for (const DockApp& app : running)
            if (keyOf(app) == key) order.push_back(key);
    }
    for (const DockApp& app : running)
        if (std::find(order.begin(), order.end(), keyOf(app)) == order.end()) order.push_back(keyOf(app));
    runningOrder_ = order;
    for (const std::wstring& key : order) {
        for (DockApp& app : running)
            if (keyOf(app) == key) apps.push_back(std::move(app));
    }
    apps_ = std::move(apps);
}
