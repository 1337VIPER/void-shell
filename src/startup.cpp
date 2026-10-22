#include "startup.h"

#include <shellapi.h>
#include <taskschd.h>
#define SECURITY_WIN32
#include <security.h>

#include <cstdio>

namespace {

constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValue[] = L"Void Shell";
constexpr wchar_t kTaskName[] = L"Void Shell";

// A BSTR that frees itself.
class Bstr {
public:
    explicit Bstr(const std::wstring& text) : value_(SysAllocString(text.c_str())) {}
    ~Bstr() { SysFreeString(value_); }
    Bstr(const Bstr&) = delete;
    Bstr& operator=(const Bstr&) = delete;
    operator BSTR() const { return value_; }

private:
    BSTR value_;
};

std::wstring XmlEscape(const std::wstring& text) {
    std::wstring out;
    for (const wchar_t c : text) {
        switch (c) {
            case L'&': out += L"&amp;"; break;
            case L'<': out += L"&lt;"; break;
            case L'>': out += L"&gt;"; break;
            case L'"': out += L"&quot;"; break;
            default: out += c;
        }
    }
    return out;
}

std::wstring HexError(HRESULT hr) {
    wchar_t text[16];
    swprintf_s(text, L"0x%08X", unsigned(hr));
    return text;
}

bool OpenTaskFolder(ComPtr<ITaskService>& service, ComPtr<ITaskFolder>& folder) {
    CHECK_HR(CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&service)));
    VARIANT empty;
    VariantInit(&empty);
    CHECK_HR(service->Connect(empty, empty, empty, empty));
    CHECK_HR(service->GetFolder(Bstr(L"\\"), &folder));
    return true;
}

bool TaskExists() {
    ComPtr<ITaskService> service;
    ComPtr<ITaskFolder> folder;
    ComPtr<IRegisteredTask> task;
    return OpenTaskFolder(service, folder) && SUCCEEDED(folder->GetTask(Bstr(kTaskName), &task));
}

bool RegisterStartupTask(std::wstring& error) {
    ComPtr<ITaskService> service;
    ComPtr<ITaskFolder> folder;
    if (!OpenTaskFolder(service, folder)) {
        error = L"Task Scheduler isn't available";
        return false;
    }
    VARIANT empty;
    VariantInit(&empty);
    ComPtr<IRegisteredTask> task;
    const HRESULT hr = folder->RegisterTask(Bstr(kTaskName), Bstr(StartupTaskXml(ExePath(), CurrentUserName())),
                                            TASK_CREATE_OR_UPDATE, empty, empty, TASK_LOGON_INTERACTIVE_TOKEN, empty,
                                            &task);
    if (FAILED(hr)) {
        error = L"Task Scheduler said " + HexError(hr);
        Log("Registering the startup task failed: 0x%08X", unsigned(hr));
        return false;
    }
    Log("Registered the administrator startup task");
    return true;
}

bool DeleteStartupTask() {
    ComPtr<ITaskService> service;
    ComPtr<ITaskFolder> folder;
    if (!OpenTaskFolder(service, folder)) return false;
    const HRESULT hr = folder->DeleteTask(Bstr(kTaskName), 0);
    return SUCCEEDED(hr) || hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
}

bool HasRunEntry() {
    DWORD size = 0;
    return RegGetValueW(HKEY_CURRENT_USER, kRunKey, kRunValue, RRF_RT_REG_SZ, nullptr, nullptr, &size) == ERROR_SUCCESS;
}

void SetRunEntry(bool on) {
    if (on) {
        const std::wstring command = L"\"" + ExePath() + L"\"";
        RegSetKeyValueW(HKEY_CURRENT_USER, kRunKey, kRunValue, REG_SZ, command.c_str(),
                        DWORD((command.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteKeyValueW(HKEY_CURRENT_USER, kRunKey, kRunValue);
    }
}

}  // namespace

bool IsElevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    TOKEN_ELEVATION elevation = {};
    DWORD size = 0;
    const bool elevated =
        GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size) && elevation.TokenIsElevated;
    CloseHandle(token);
    return elevated;
}

std::wstring ExePath() {
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return path;
}

std::wstring CurrentUserName() {
    wchar_t name[256] = {};
    ULONG size = ARRAYSIZE(name);
    if (GetUserNameExW(NameSamCompatible, name, &size)) return name;
    DWORD plainSize = ARRAYSIZE(name);
    return GetUserNameW(name, &plainSize) ? name : L"";
}

StartupStatus ReadStartup() { return {HasRunEntry(), TaskExists()}; }

std::wstring ApplyStartup(bool startAtLogin, bool runAsAdmin) {
    if (!startAtLogin) {
        SetRunEntry(false);
        if (TaskExists() && !DeleteStartupTask())
            return L"The administrator startup task is still there. Restart as administrator to remove it.";
        return L"";
    }
    if (!runAsAdmin) {
        SetRunEntry(true);
        if (TaskExists() && !DeleteStartupTask())
            return L"An old administrator startup task remains; it goes once Void Shell runs as administrator.";
        return L"";
    }
    // Starting elevated at sign-in needs the scheduled task, which only an elevated Void Shell can create.
    // Until then the Run entry keeps it starting, just without administrator rights.
    if (!IsElevated()) {
        SetRunEntry(true);
        return L"Restart as administrator to finish: it starts without admin rights until then.";
    }
    std::wstring error;
    if (!RegisterStartupTask(error)) {
        SetRunEntry(true);
        return L"Couldn't create the startup task (" + error + L").";
    }
    SetRunEntry(false);
    return L"";
}

std::wstring StartupTaskXml(const std::wstring& exePath, const std::wstring& user) {
    const std::wstring who = XmlEscape(user);
    return L"<?xml version=\"1.0\" encoding=\"UTF-16\"?>\n"
           L"<Task version=\"1.2\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">\n"
           L"  <RegistrationInfo>\n"
           L"    <Description>Starts Void Shell with administrator rights when you sign in.</Description>\n"
           L"  </RegistrationInfo>\n"
           L"  <Triggers>\n"
           L"    <LogonTrigger><Enabled>true</Enabled><UserId>" + who + L"</UserId><Delay>PT3S</Delay></LogonTrigger>\n"
           L"  </Triggers>\n"
           L"  <Principals>\n"
           L"    <Principal id=\"Author\"><UserId>" + who +
           L"</UserId><LogonType>InteractiveToken</LogonType><RunLevel>HighestAvailable</RunLevel></Principal>\n"
           L"  </Principals>\n"
           L"  <Settings>\n"
           L"    <MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>\n"
           L"    <DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>\n"
           L"    <StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>\n"
           L"    <ExecutionTimeLimit>PT0S</ExecutionTimeLimit>\n"
           L"    <Priority>4</Priority>\n"
           L"  </Settings>\n"
           L"  <Actions Context=\"Author\">\n"
           L"    <Exec><Command>\"" + XmlEscape(exePath) + L"\"</Command></Exec>\n"
           L"  </Actions>\n"
           L"</Task>\n";
}

bool ValidateTaskXml(const std::wstring& xml, std::wstring& error) {
    ComPtr<ITaskService> service;
    ComPtr<ITaskFolder> folder;
    if (!OpenTaskFolder(service, folder)) {
        error = L"Task Scheduler isn't available";
        return false;
    }
    ComPtr<ITaskDefinition> definition;
    HRESULT hr = service->NewTask(0, &definition);
    if (SUCCEEDED(hr)) hr = definition->put_XmlText(Bstr(xml));
    if (FAILED(hr)) {
        error = HexError(hr);
        return false;
    }
    return true;
}

bool RestartElevated() {
    const std::wstring exe = ExePath();
    SHELLEXECUTEINFOW info = {sizeof(info)};
    info.fMask = SEE_MASK_NOASYNC;
    info.lpVerb = L"runas";
    info.lpFile = exe.c_str();
    info.lpParameters = L"--replace";
    info.nShow = SW_SHOWNORMAL;
    if (ShellExecuteExW(&info)) return true;
    Log(GetLastError() == ERROR_CANCELLED ? "Restart as administrator was declined" : "Restart as administrator failed");
    return false;
}
