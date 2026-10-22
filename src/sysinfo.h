#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "common.h"

// One reading of the machine. Anything this PC can't report stays at -1 and shows as a dash.
struct SystemStats {
    bool valid = false;     // at least one full reading has been taken
    float cpu = -1.0f;      // percent of all cores
    float cpuTemp = -1.0f;  // °C
    float gpu = -1.0f;      // percent of the busiest engine, as Task Manager shows it
    float gpuTemp = -1.0f;  // °C
    float ram = -1.0f;      // percent in use
    float ramUsedGb = 0.0f;
    float ramTotalGb = 0.0f;
    float vram = -1.0f;     // the main GPU's dedicated memory, percent in use
    float vramUsedGb = 0.0f;
    float vramTotalGb = 0.0f;
};

// Reads CPU, GPU and memory use and temperatures without any driver of its own:
// - CPU and memory use: Windows (GetSystemTimes, GlobalMemoryStatusEx).
// - GPU use and video memory: Windows' GPU performance counters, the ones Task Manager reads.
// - GPU temperature: the display driver's own report (D3DKMT adapter performance data, WDDM 2.4+).
// - CPU temperature: the ACPI thermal zone if the PC has one, else LibreHardwareMonitor or OpenHardwareMonitor
//   while one of them is running. Windows has no CPU temperature of its own on most desktops.
class SystemMonitor {
public:
    SystemMonitor();
    ~SystemMonitor();

    // Samples once a second on a thread of its own while active and posts `message` to `notify` after each
    // reading. Pausing keeps the thread (so the void never waits for it); Stop ends it.
    void SetActive(bool active, HWND notify, UINT message);
    void Stop();
    SystemStats Latest() const;

    // Direct use on the calling thread (the self-test). The thread must have COM initialized.
    bool Open();
    SystemStats Sample();
    void Close();
    // Which sources answered, for the log ("cpu temp: LibreHardwareMonitor" and so on).
    std::wstring SourceSummary() const;

private:
    struct Readers;
    void Run();

    std::unique_ptr<Readers> readers_;
    std::thread thread_;
    HANDLE stop_ = nullptr;
    HANDLE wake_ = nullptr;
    std::atomic<bool> active_{false};
    std::atomic<HWND> notify_{nullptr};
    std::atomic<UINT> message_{0};
    mutable std::mutex mutex_;
    SystemStats latest_;
};
