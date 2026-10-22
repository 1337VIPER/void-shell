#pragma once

#include <string>

// Headless check: renders the void offscreen with three fake desktops, verifies that zooming out of one and into
// another both start and end pixel-identical to those desktops, times a fully open frame and (with fake desktops
// only) saves PNG frames. Writes report.txt into outDir. Returns 0 on pass.
int RunRenderTest(const std::wstring& outDir, bool useCapture);

// Offscreen check of the shell frame and zones on a simulated 2560x1440 monitor: paints the frame bars over a fake
// desktop, checks the bar and zone geometry and the top-bar hit areas, and saves PNGs. Registers no appbars and
// moves no windows. Writes frame.txt into outDir. Returns 0 on pass.
int RunFrameTest(const std::wstring& outDir);

// Offscreen check of the settings page: paints it (normal and while recording a shortcut, at two scales), checks
// every control can be clicked, and round-trips the settings through a test ini in outDir. Returns 0 on pass.
int RunSettingsTest(const std::wstring& outDir);

// Read-only check of start-at-login: builds the scheduled task definition and has Task Scheduler parse it
// without registering anything; reports the current startup entries. Returns 0 on pass.
int RunStartupTest(const std::wstring& outDir);

// Read-only check of the virtual desktop API on this machine: lists desktops and cross-checks them against the
// registry. Creates, switches and changes nothing. Writes desktops.txt into outDir. Returns 0 on pass.
int RunDesktopsTest(const std::wstring& outDir);

// Offscreen check of the dock: lays it out at two scales and checks every part can be hit, paints it in the wide
// frame's bottom margin with an app preview above it, then reads this PC's taskbar pins, open apps, icons and volume
// without changing anything (it logs counts only, never window titles). Writes dock.txt into outDir. Returns 0 on pass.
int RunDockTest(const std::wstring& outDir);

// Offscreen check of the system orbs: where they sit for every frame size, renders over the wide margin's rain,
// then a read-only reading of this PC (CPU, GPU and memory use, temperatures) and a check that the background
// reader pauses and stops cleanly. Writes sysinfo.txt into outDir. Returns 0 on pass.
int RunSysInfoTest(const std::wstring& outDir);

// Offscreen check of the margin's folder and app bubbles and the app picker: sizes for every frame, the + button,
// where bubbles may go and how they settle around the orbs, the dock and each other, a render with real icons, saving
// and loading, and the picker's search, keys and clicks. Reads Start's app list without changing anything.
// Writes bubbles.txt into outDir. Returns 0 on pass.
int RunBubblesTest(const std::wstring& outDir);
