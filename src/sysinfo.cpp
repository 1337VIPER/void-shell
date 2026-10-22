#include "sysinfo.h"

#include <winternl.h>  // NTSTATUS, which the display driver header needs

#include <d3dkmthk.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <wbemidl.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

namespace {

constexpr float kGiB = 1024.0f * 1024.0f * 1024.0f;
constexpr int kMonitorRetrySamples = 30;  // look for a hardware monitor app again every 30 s

ULONGLONG Ticks(const FILETIME& time) { return (ULONGLONG(time.dwHighDateTime) << 32) | time.dwLowDateTime; }

std::wstring Lower(std::wstring text) {
    if (!text.empty()) CharLowerBuffW(text.data(), DWORD(text.size()));
    return text;
}

// Every instance of a wildcard counter ("\GPU Engine(*)\..."), with lower-case instance names.
std::vector<std::pair<std::wstring, double>> ReadInstances(PDH_HCOUNTER counter) {
    std::vector<std::pair<std::wstring, double>> values;
    if (!counter) return values;
    DWORD size = 0;
    DWORD count = 0;
    const DWORD format = PDH_FMT_DOUBLE | PDH_FMT_NOCAP100;
    if (PdhGetFormattedCounterArrayW(counter, format, &size, &count, nullptr) != PDH_MORE_DATA || !size) return values;
    std::vector<BYTE> buffer(size);
    auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());
    if (PdhGetFormattedCounterArrayW(counter, format, &size, &count, items) != ERROR_SUCCESS) return values;
    for (DWORD i = 0; i < count; ++i) {
        const DWORD status = items[i].FmtValue.CStatus;
        if (status == PDH_CSTATUS_VALID_DATA || status == PDH_CSTATUS_NEW_DATA)
            values.push_back({Lower(items[i].szName), items[i].FmtValue.doubleValue});
    }
    return values;
}

PDH_HCOUNTER AddCounter(PDH_HQUERY query, const wchar_t* path) {
    PDH_HCOUNTER counter = nullptr;
    return PdhAddEnglishCounterW(query, path, 0, &counter) == ERROR_SUCCESS ? counter : nullptr;
}

}  // namespace

struct SystemMonitor::Readers {
    // CPU use, from the previous reading.
    ULONGLONG idle = 0, kernel = 0, user = 0;
    bool haveTimes = false;

    // Windows' performance counters.
    PDH_HQUERY query = nullptr;
    PDH_HCOUNTER engines = nullptr;  // \GPU Engine(*)\Utilization Percentage
    PDH_HCOUNTER memory = nullptr;   // \GPU Adapter Memory(*)\Dedicated Usage
    PDH_HCOUNTER thermal = nullptr;  // \Thermal Zone Information(*)\Temperature (kelvin)

    // The main GPU: the hardware adapter with the most video memory.
    std::wstring gpuName;
    std::wstring luid;  // as the counter instances spell it: "luid_0x00000000_0x0000d1f2"
    float vramTotalGb = 0.0f;

    // The display driver's own temperature report.
    PFND3DKMT_QUERYADAPTERINFO queryAdapter = nullptr;
    PFND3DKMT_CLOSEADAPTER closeAdapter = nullptr;
    D3DKMT_HANDLE adapter = 0;

    // LibreHardwareMonitor / OpenHardwareMonitor, while one is running.
    ComPtr<IWbemServices> hardwareMonitor;
    std::wstring hardwareMonitorName;
    int retryIn = 0;

    std::wstring cpuTempSource;
    std::wstring gpuTempSource;
};

SystemMonitor::SystemMonitor() = default;

SystemMonitor::~SystemMonitor() {
    Stop();
    Close();
}

bool SystemMonitor::Open() {
    Close();
    readers_ = std::make_unique<Readers>();
    Readers& r = *readers_;

    ComPtr<IDXGIFactory1> factory;
    LUID luid = {};
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        SIZE_T most = 0;
        ComPtr<IDXGIAdapter1> adapter;
        for (UINT i = 0; factory->EnumAdapters1(i, adapter.ReleaseAndGetAddressOf()) != DXGI_ERROR_NOT_FOUND; ++i) {
            DXGI_ADAPTER_DESC1 desc = {};
            if (FAILED(adapter->GetDesc1(&desc)) || (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) continue;
            if (desc.DedicatedVideoMemory <= most) continue;
            most = desc.DedicatedVideoMemory;
            luid = desc.AdapterLuid;
            r.gpuName = desc.Description;
        }
        if (most) {
            wchar_t name[64];
            swprintf_s(name, L"luid_0x%08x_0x%08x", unsigned(luid.HighPart), unsigned(luid.LowPart));
            r.luid = name;
            r.vramTotalGb = float(double(most) / kGiB);
        }
    }

    if (PdhOpenQueryW(nullptr, 0, &r.query) == ERROR_SUCCESS) {
        r.engines = AddCounter(r.query, L"\\GPU Engine(*)\\Utilization Percentage");
        r.memory = AddCounter(r.query, L"\\GPU Adapter Memory(*)\\Dedicated Usage");
        r.thermal = AddCounter(r.query, L"\\Thermal Zone Information(*)\\Temperature");
        PdhCollectQueryData(r.query);  // rates need a reading to compare against
    } else {
        r.query = nullptr;
    }

    // gdi32 is already loaded (it's linked), so this finds Windows' own copy.
    if (HMODULE gdi = GetModuleHandleW(L"gdi32.dll"); gdi && !r.luid.empty()) {
        const auto open = reinterpret_cast<PFND3DKMT_OPENADAPTERFROMLUID>(GetProcAddress(gdi, "D3DKMTOpenAdapterFromLuid"));
        r.queryAdapter = reinterpret_cast<PFND3DKMT_QUERYADAPTERINFO>(GetProcAddress(gdi, "D3DKMTQueryAdapterInfo"));
        r.closeAdapter = reinterpret_cast<PFND3DKMT_CLOSEADAPTER>(GetProcAddress(gdi, "D3DKMTCloseAdapter"));
        D3DKMT_OPENADAPTERFROMLUID request = {};
        request.AdapterLuid = luid;
        if (open && r.queryAdapter && r.closeAdapter && open(&request) >= 0) r.adapter = request.hAdapter;
    }

    FILETIME idle, kernel, user;
    if (GetSystemTimes(&idle, &kernel, &user)) {
        r.idle = Ticks(idle);
        r.kernel = Ticks(kernel);
        r.user = Ticks(user);
        r.haveTimes = true;
    }
    return r.haveTimes;
}

void SystemMonitor::Close() {
    if (!readers_) return;
    Readers& r = *readers_;
    if (r.query) PdhCloseQuery(r.query);
    if (r.adapter && r.closeAdapter) {
        D3DKMT_CLOSEADAPTER request = {r.adapter};
        r.closeAdapter(&request);
    }
    readers_.reset();
}

namespace {

// LibreHardwareMonitor and OpenHardwareMonitor publish their sensors through WMI while they run.
ComPtr<IWbemServices> ConnectHardwareMonitor(std::wstring& name) {
    ComPtr<IWbemLocator> locator;
    if (FAILED(CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&locator)))) return nullptr;
    for (const wchar_t* space : {L"ROOT\\LibreHardwareMonitor", L"ROOT\\OpenHardwareMonitor"}) {
        BSTR path = SysAllocString(space);
        ComPtr<IWbemServices> services;
        const HRESULT hr = locator->ConnectServer(path, nullptr, nullptr, nullptr, 0, nullptr, nullptr, &services);
        SysFreeString(path);
        if (FAILED(hr)) continue;
        CoSetProxyBlanket(services.Get(), RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_CALL,
                          RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
        name = wcschr(space, L'\\') + 1;
        return services;
    }
    return nullptr;
}

// The CPU package (or Tctl/Tdie) temperature from a hardware monitor app; -1 if it has none, -2 if it has gone.
float HardwareMonitorCpuTemp(IWbemServices* services) {
    BSTR language = SysAllocString(L"WQL");
    BSTR query = SysAllocString(L"SELECT Identifier, Name, Value FROM Sensor WHERE SensorType='Temperature'");
    ComPtr<IEnumWbemClassObject> results;
    const HRESULT hr = services->ExecQuery(language, query, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                                           nullptr, &results);
    SysFreeString(language);
    SysFreeString(query);
    if (FAILED(hr)) return -2.0f;
    float best = -1.0f;
    int bestRank = -1;
    for (;;) {
        ComPtr<IWbemClassObject> sensor;
        ULONG returned = 0;
        if (FAILED(results->Next(2000, 1, &sensor, &returned)) || returned == 0) break;
        VARIANT identifier, name, value;
        VariantInit(&identifier);
        VariantInit(&name);
        VariantInit(&value);
        if (SUCCEEDED(sensor->Get(L"Identifier", 0, &identifier, nullptr, nullptr)) && identifier.vt == VT_BSTR &&
            SUCCEEDED(sensor->Get(L"Name", 0, &name, nullptr, nullptr)) && name.vt == VT_BSTR &&
            SUCCEEDED(sensor->Get(L"Value", 0, &value, nullptr, nullptr)) &&
            SUCCEEDED(VariantChangeType(&value, &value, 0, VT_R4))) {
            const std::wstring id = Lower(identifier.bstrVal);
            const std::wstring label = Lower(name.bstrVal);
            if (id.find(L"cpu") != std::wstring::npos && value.fltVal > 1.0f && value.fltVal < 125.0f) {
                // The package sensor is the one people quote; then an average; then the hottest core.
                int rank = 0;
                if (label.find(L"package") != std::wstring::npos || label.find(L"tctl") != std::wstring::npos ||
                    label.find(L"tdie") != std::wstring::npos)
                    rank = 2;
                else if (label.find(L"average") != std::wstring::npos)
                    rank = 1;
                if (rank > bestRank || (rank == bestRank && value.fltVal > best)) {
                    bestRank = rank;
                    best = value.fltVal;
                }
            }
        }
        VariantClear(&identifier);
        VariantClear(&name);
        VariantClear(&value);
    }
    return best;
}

}  // namespace

SystemStats SystemMonitor::Sample() {
    SystemStats stats;
    if (!readers_) return stats;
    Readers& r = *readers_;

    FILETIME idle, kernel, user;
    if (GetSystemTimes(&idle, &kernel, &user)) {
        const ULONGLONG i = Ticks(idle), k = Ticks(kernel), u = Ticks(user);
        // Kernel time includes idle time.
        const ULONGLONG total = (k - r.kernel) + (u - r.user);
        if (r.haveTimes && total > 0)
            stats.cpu = std::clamp(100.0f * (1.0f - float(double(i - r.idle) / double(total))), 0.0f, 100.0f);
        r.idle = i;
        r.kernel = k;
        r.user = u;
        r.haveTimes = true;
    }

    MEMORYSTATUSEX memory = {sizeof(memory)};
    if (GlobalMemoryStatusEx(&memory) && memory.ullTotalPhys) {
        stats.ramTotalGb = float(double(memory.ullTotalPhys) / kGiB);
        stats.ramUsedGb = float(double(memory.ullTotalPhys - memory.ullAvailPhys) / kGiB);
        stats.ram = 100.0f * stats.ramUsedGb / stats.ramTotalGb;
    }

    if (r.query && PdhCollectQueryData(r.query) == ERROR_SUCCESS) {
        // GPU use: add up each engine's use across processes, then take the busiest engine (as Task Manager does).
        std::map<std::wstring, double> engines;
        bool any = false;
        for (const auto& [name, value] : ReadInstances(r.engines)) {
            if (!r.luid.empty() && name.find(r.luid) == std::wstring::npos) continue;
            const size_t at = name.find(L"luid_");
            if (at == std::wstring::npos) continue;
            engines[name.substr(at)] += value;  // drop the "pid_1234_" prefix
            any = true;
        }
        if (any) {
            double busiest = 0.0;
            for (const auto& [engine, value] : engines) busiest = std::max(busiest, value);
            stats.gpu = float(std::clamp(busiest, 0.0, 100.0));
        }
        for (const auto& [name, bytes] : ReadInstances(r.memory)) {
            if (r.luid.empty() || name.find(r.luid) == std::wstring::npos || r.vramTotalGb <= 0.0f) continue;
            stats.vramTotalGb = r.vramTotalGb;
            stats.vramUsedGb = float(bytes / kGiB);
            stats.vram = std::clamp(100.0f * stats.vramUsedGb / stats.vramTotalGb, 0.0f, 100.0f);
        }
        double hottest = -1.0;
        for (const auto& [name, kelvin] : ReadInstances(r.thermal)) hottest = std::max(hottest, kelvin - 273.15);
        if (hottest > 5.0 && hottest < 125.0) {
            stats.cpuTemp = float(hottest);
            r.cpuTempSource = L"ACPI thermal zone";
        }
    }

    if (r.adapter) {
        D3DKMT_ADAPTER_PERFDATA perf = {};
        D3DKMT_QUERYADAPTERINFO request = {};
        request.hAdapter = r.adapter;
        request.Type = KMTQAITYPE_ADAPTERPERFDATA;
        request.pPrivateDriverData = &perf;
        request.PrivateDriverDataSize = sizeof(perf);
        if (r.queryAdapter(&request) >= 0 && perf.Temperature > 50 && perf.Temperature < 1250) {
            stats.gpuTemp = float(perf.Temperature) / 10.0f;
            r.gpuTempSource = L"display driver";
        }
    }

    if (stats.cpuTemp < 0.0f) {
        if (!r.hardwareMonitor && --r.retryIn <= 0) {
            r.retryIn = kMonitorRetrySamples;
            r.hardwareMonitor = ConnectHardwareMonitor(r.hardwareMonitorName);
        }
        if (r.hardwareMonitor) {
            const float temp = HardwareMonitorCpuTemp(r.hardwareMonitor.Get());
            if (temp == -2.0f) r.hardwareMonitor.Reset();  // the app was closed
            if (temp > 0.0f) {
                stats.cpuTemp = temp;
                r.cpuTempSource = r.hardwareMonitorName;
            }
        }
    }
    stats.valid = stats.cpu >= 0.0f;
    return stats;
}

std::wstring SystemMonitor::SourceSummary() const {
    if (!readers_) return L"not open";
    const Readers& r = *readers_;
    std::wstring text = L"GPU: " + (r.gpuName.empty() ? std::wstring(L"none found") : r.gpuName);
    text += L"; counters: ";
    text += r.query ? L"open" : L"unavailable";
    text += r.engines ? L", GPU use" : L"";
    text += r.memory ? L", video memory" : L"";
    text += r.thermal ? L", thermal zones" : L"";
    text += L"; GPU temperature: " + (r.gpuTempSource.empty() ? std::wstring(L"none") : r.gpuTempSource);
    text += L"; CPU temperature: " + (r.cpuTempSource.empty() ? std::wstring(L"none") : r.cpuTempSource);
    return text;
}

SystemStats SystemMonitor::Latest() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return latest_;
}

void SystemMonitor::SetActive(bool active, HWND notify, UINT message) {
    notify_ = notify;
    message_ = message;
    active_ = active;
    if (active && !thread_.joinable()) {
        stop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        wake_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        thread_ = std::thread(&SystemMonitor::Run, this);
    }
    if (wake_) SetEvent(wake_);
}

void SystemMonitor::Stop() {
    if (thread_.joinable()) {
        SetEvent(stop_);
        thread_.join();
    }
    for (HANDLE* handle : {&stop_, &wake_}) {
        if (*handle) CloseHandle(*handle);
        *handle = nullptr;
    }
}

void SystemMonitor::Run() {
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool open = false;
    bool primed = false;
    const HANDLE events[2] = {stop_, wake_};
    for (;;) {
        const DWORD timeout = !active_ ? INFINITE : (primed ? 1000 : 0);
        const DWORD woke = WaitForMultipleObjects(2, events, FALSE, timeout);
        if (woke == WAIT_OBJECT_0) break;
        if (!active_) {
            primed = false;
            continue;
        }
        if (woke == WAIT_OBJECT_0 + 1 && primed) continue;  // nudged while running: keep the one-second rhythm
        if (!open) open = Open();
        if (!primed) {
            // After a pause the last CPU and counter readings are stale: take a fresh baseline first.
            Sample();
            primed = true;
            if (WaitForSingleObject(stop_, 500) == WAIT_OBJECT_0) break;
        }
        const SystemStats stats = Sample();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            latest_ = stats;
        }
        if (const HWND notify = notify_) PostMessageW(notify, message_, 0, 0);
    }
    Close();
    if (SUCCEEDED(com)) CoUninitialize();
}
