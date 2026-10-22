# Void Shell · VenomMind (Phase 9: sockets and taskbar-style app bubbles)

> **Running unsigned builds:** builds from this source aren't code-signed. With Windows **Smart App Control** on, a fresh build can be refused ("An Application Control policy has blocked this file"). Whether to change that setting is up to you; Void Shell itself never asks for it to be turned off.

A new wrapper around the Windows desktop, branded with Venom Board's look (its mark, palette and type), under the name **VenomMind**:

- **Sockets and taskbar-style app bubbles (Phase 9):**
  - **Sockets:** every band has a row of solid, evenly spaced sockets showing where bubbles can go: sheen-blue rims along the top and bottom for folders, crimson rims down the sides for apps. Bubbles sit in sockets and snap to the nearest free one when dropped. Dropping on a taken socket swaps the two bubbles. While you drag, every socket the bubble may go to turns into a glowing **+**, and the one it will land in fills in. Sockets skip the system orbs and the dock. "Show empty sockets" in Settings → Frame hides them except while dragging.
  - **App bubbles act like taskbar icons:** a short line under a bubble means its app is running, a crimson line means it's in front. Clicking an open app brings it up, or minimizes it if it's already in front; with several windows each click brings up the next. Middle-click starts another one. Right-click adds "New window" and "Close window(s)".
  - **Previews:** hover an app bubble whose app is open and live thumbnails of its windows open beside it, toward the desktop, as on the taskbar. Click one to go to it, or its × to close it. The dock and the bubbles share the same preview window.
- **Folder and app bubbles (Phase 8):** quick access from the wide margin.
  - **Adding:** click the **+** next to the gear on the frame's top strip, then **Add a folder…** (Windows' folder picker) or **Add an app…** (a VenomMind list of every app in Start, with real icons). In the app list, type to search, use the arrow keys and Enter, or click. **Browse for a program…** picks any exe or shortcut instead.
  - **Where they go:** apps in the left and right bands, folders in the top and bottom bands. Each new one takes the first free socket (apps from the top of the left band, folders from the left of the top band) on the monitor you clicked the + on.
  - **Moving:** drag a bubble to any socket of its kind, on any monitor with the frame.
  - **Using:** click to open the folder or start the app. Right-click to open it, show it in its folder, or remove it from the margin.
  - **Look:** glass bubbles with the item's own icon; apps have a crimson rim, folders a sheen-blue one. Names show under them in the medium and large margins, or beside them on hover in the small margin.
  - They need the wide margin (the slim frame has no room). Each bubble remembers its monitor and its place in its band, so it comes back where you left it.
- **System orbs (Phase 7):** CPU, GPU, RAM and VRAM in glass orbs at the top of the frame, floating over the rain. Each orb's ring fills with use, running from sheen blue to crimson. The CPU and GPU orbs also show their temperature, in green, then amber above 65 °C, then crimson above 82 °C.
  - **Where they go:** the medium and large margins get orbs. The small margin, and the slim frame's strip, get a row of compact chips instead.
  - Click them for Task Manager.
  - They update once a second, on a thread of their own, only while the frame is showing (not while the PC is locked or the void is open).
  - **CPU temperature:** Windows has no CPU temperature of its own on most desktops, this one included, so it shows a dash. It appears when the PC has an ACPI thermal zone, or while LibreHardwareMonitor or OpenHardwareMonitor is running.
  - **Turn them off:** Settings → Frame → System info.
- **One window (Phase 7):** a zone layout with a single panel. Every window you drop, or that opens with "Place new windows in zones" on, fills the frame's opening. It's first in the layout list. Your saved layout choices keep pointing at the same layouts.
- **The dock (Phase 6):** a taskbar of VenomMind's own, in the frame's bottom margin on the main monitor, in Venom glass with a crimson-to-blue rim.
  - **Start** (Win+X menu on right-click) and **search**.
  - **Apps:** pinned apps first, then running ones, grouped the way the Windows taskbar groups them (by app ID, else by exe; Store-style apps by their package). A short line under an app means it's running and a crimson one means it has the focus. An app asking for attention pulses.
  - **Clicking an app** opens it, brings it up, puts it away if it's already in front, or cycles through its windows. Middle-click opens a new one.
  - **Right-click an app** to pin or unpin it, open a new window or close its windows.
  - **Hover an app** for live previews of its windows (drawn by Windows itself). Click a preview to go there, or its **×** to close that window.
  - **Desktops:** one dot per desktop, the current one a glowing blue bar. Click one to switch, or scroll over them.
  - **Volume:** scroll over it to change it by 2%, click to mute, right-click for sound settings.
  - **Clock:** click for notifications and the calendar (Win+N), right-click for date and time settings.
  - **Which windows:** the current desktop's by default, or every desktop's.
  - **Pins:** the first time, the dock copies what's pinned to the Windows taskbar. After that its pins are its own (saved in the ini). "Pin the Windows taskbar's apps again" copies them afresh.
  - The bottom band grows to fit the dock where needed (the slim frame, or the small wide margin). The dock needs the frame and steps aside for fullscreen games, as the frame does.
- **Hide the Windows taskbar (Phase 6, optional, off by default):** turns on Windows' taskbar auto-hide while the dock is on, so the dock is your taskbar. Your own auto-hide setting is remembered first and put back when you turn this off, turn the dock off, or quit (also on sign-out and after a crash).
- **Wide margin with matrix rain (Phase 5):** a broad frame around the desktop (small, medium or large), with code raining through it from every edge toward the desktop.
  - The code is mirrored katakana, digits and symbols, in Venom crimson, green or sheen blue.
  - Every cell keeps a faint glyph (a resting wall of code), with bright streams and white heads falling through it.
  - It runs at 20 steps a second and costs about 3 ms of CPU per step on this PC (around 6% of one core) while visible.
  - It pauses while the PC is locked, on battery, while the void is open or while a game is fullscreen.
  - The slim frame from Phase 3 is still a choice.
- **Venom branding (Phase 5):** the Venom mark (drawn as vectors from Venom Board's `icon.svg`) is the tray icon and sits on the frame's top strip and in settings, with the wordmark (`BrandName` in the ini, default "VenomMind").
  - The colours follow Venom Board's tokens: void `#060609`, fang crimson `#ff2a4f`, sheen blue `#4157ff`, bone `#f1eef2`.
  - The void's nebula, halos and plus world use the same palette.
- **Removing and reordering desktops (Phase 5):** in the void, hover a world for a small **×** badge. Click it once to arm, and again within 3 seconds to remove that desktop; its windows move to the neighbouring desktop and nothing closes. Drag a world left or right to change the desktop order.
- **Fresher pictures (Phase 5):** when you switch desktops without the void (Task View, Win+Ctrl+arrows), the desktop you land on is re-captured a moment later. The current desktop is also refreshed every minute.

**About the Windows taskbar:** Windows always pins its taskbar to the screen's very edge, so it sits outside the wide margin, not inside it. The dock is the taskbar inside the margin. Turn on "Hide the Windows taskbar" and the margin runs all the way round, with Windows' taskbar sliding in over it only when you push the mouse to the bottom edge.

Everything from Phases 1–4:

- **The void:** press **Ctrl+Alt+Space** (or your own shortcut) and your desktop curls into a glowing ball world as the camera pulls back into a starfield. Each Windows virtual desktop is a world, and a **plus world** sits at the end.
  - Click a world (or press **1–9**) to fly into it.
  - Click the plus world to make a new desktop and fly in.
  - Press **Esc** to go back.
  - Your other monitors dim while the void is open.
- **The shell frame:** a thin glowing frame around each monitor (or just the main one). Windows keeps maximized windows inside it. Its top bar shows the current desktop's name.
  - Click the **orb** or the name to open the void.
  - Click the **gear** for settings.
  - Click the **layout button** to pick this desktop's zone layout.
- **Zones:** drag a window and the zones fade in. Let go and it snaps into the zone under the mouse; hold **Shift** to place it freely.
  - **Each desktop remembers its own layout.** Desktops you haven't set use the default.
  - Optionally, new windows go straight into the first free zone.
- **Games come first:** while a game or video fills a screen, the shortcut, snapping and the frame on that screen pause, so nothing of Void Shell's can interfere.

It all runs on top of the normal Windows desktop. Explorer and the Start menu are untouched, the Windows taskbar only auto-hides if you ask for it, and the desktops are Windows' own.

## Run it

```bat
build.bat
build\void-shell.exe
```

An orb appears in the system tray. Left-click it to open the void. Right-click it for **Settings…**, quick toggles, this desktop's zone layout, and **Quit**.

### Settings window

Five tabs: The void, Frame, Dock, Zones, Startup (Tab key switches).

- **Shortcut:** click the box and press a new combination. Esc cancels; Backspace restores Ctrl+Alt+Space.
- **Step aside for fullscreen games and videos.**
- **Shell frame:** on or off, Slim or Wide margin, margin size, matrix rain and its colour, on all monitors or the main one, and the system orbs.
- **Dock:** on or off, apps from this desktop or all desktops, hide the Windows taskbar, and "Pin the Windows taskbar's apps again".
- **Zones:** snap on drag, place new windows, the layout for this desktop and the default for the others, and "Give every desktop this layout".
- **Start when I sign in**, **Run as administrator** (with a **Restart as administrator** button).
- **Open log** and **Open folder** links.

Choices are saved in `build\void-shell.ini`. While nothing is happening Void Shell sleeps: no CPU or GPU use except a once-a-second check of which desktop is showing and whether a game is in front. It writes `build\void-shell.log`.

If a copy is already running, `build.bat` renames it to `void-shell.old.exe` so the build can finish. Quit the old copy from the tray before starting the new one.

### Start at sign-in and administrator mode

- **Normal:** "Start when I sign in" adds a `Void Shell` entry under `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`.
- **Why administrator mode:** Windows doesn't let a normal app move windows of apps running as administrator. When a window can't be snapped for that reason, Void Shell says so once from the tray.
- **Turning it on:** with **Run as administrator** on, click **Restart as administrator**. Windows asks once, and the elevated copy takes over from the running one.
- **Starting elevated at sign-in:** a Run entry can't start an elevated app without a prompt each time. So in administrator mode, start-at-sign-in uses a scheduled task named **Void Shell** that runs with highest privileges. It never times out, ignores battery state, and starts 3 seconds after sign-in. Only an elevated Void Shell can create that task. Until then the Run entry keeps it starting, and the settings window says so.
- **Moving the folder:** if you move it, open Void Shell once from the new place and the startup entry follows.

Needs Windows 10/11, a Direct3D 11 GPU and the Visual C++ Build Tools (Windows SDK included). Nothing else. Workspaces use Windows 11 24H2 or later (build 26100+) at full speed, and fall back on older builds.

## How it works

### The void
- **Capture:** the monitor under the mouse is grabbed (Desktop Duplication, GDI as fallback). The picture is kept per desktop *and per monitor*, so opening on another screen doesn't throw pictures away.
- **Seamless cover:** a cloaked full-screen window shows that picture at exactly 1:1, then it's uncloaked. A vertex shader curls it into a sphere while the camera pulls back.
- **Other monitors:** they get a dark click-through veil that fades in with the zoom. It also hides Windows' own desktop-switch slide there.
- **Entering another desktop:** Windows switches desktops under the overlay, which is excluded from capture. The new desktop is captured through it and cross-faded onto its ball, so the zoom lands on what's really there.

### The shell frame
- **Bands:** four appbars per monitor (`SHAppBarMessage`), so the work area shrinks and maximized windows stop at the frame.
- **Drawing:** layered windows painted on the CPU. The overhang that draws the rounded corners is transparent, so clicks pass through.
- **Fullscreen:** the frame hides when the shell reports a fullscreen app, and also when Void Shell's own detection sees one on that monitor.
- **Cleanup:** the reserved space is handed back on quit, crash or sign-out.

### The bubbles
- **Drawing:** each bubble is a small layer drawn over its band's rain, like the orbs. A dragged bubble is carried by its own click-through window, so it can cross from one band or monitor to another.
- **Sockets:** as many as fit along each band with at least 12 px between them, spread so every gap is equal, centred across the band; any that would overlap the orbs or the dock are left out.
- **Placing:** a bubble's place is saved as a fraction of its band (across and along), plus its monitor's device name, so it survives margin and resolution changes. On every layout each bubble takes the free socket nearest that place; if its band is full it moves to the other band of its pair.
- **Taskbar behaviour:** an app bubble's windows are found the way the dock finds an app's (its app ID, its package, or its exe), on the current desktop, checked every 1.5 s and on each click. Start entries for desktop apps without an app ID are named by their exe's path under a known folder (`{GUID}\Vendor\app.exe`); those are turned back into the real path so they match their windows.
- **The app list:** read from Start's Applications folder (`FOLDERID_AppsFolder`) each time it opens (about 0.4 s for 160 apps here), without uninstallers, help files and web links. Apps open through `shell:AppsFolder\<id>`, just as Start opens them.
- **Opening:** like the dock, through Explorer when Void Shell runs as administrator, so nothing inherits administrator rights.

### The system orbs
- **CPU and memory use:** `GetSystemTimes` and `GlobalMemoryStatusEx`.
- **GPU use and video memory:** Windows' own GPU performance counters (`\GPU Engine(*)\Utilization Percentage`, `\GPU Adapter Memory(*)\Dedicated Usage`), the ones Task Manager reads. GPU use is each engine's use added up across processes, then the busiest engine, as Task Manager shows it. The main GPU is the hardware adapter with the most video memory.
- **GPU temperature:** the display driver's own report (`D3DKMTQueryAdapterInfo` with adapter performance data, WDDM 2.4 and later), as in Task Manager. It works on NVIDIA, AMD and Intel without any vendor library.
- **CPU temperature:** the ACPI thermal zone counter if the PC has one. Otherwise LibreHardwareMonitor's or OpenHardwareMonitor's WMI sensors while one of them runs: the package (or Tctl/Tdie) sensor, else the average, else the hottest core. Void Shell never loads a driver of its own for this.
- **Drawing:** the orbs are painted into their own small layer on the top bar, composed over the rain each frame, so the rain falls behind them.
- **Cost:** one reading takes about 25 ms on its own thread, once a second. Pausing (locked, void open) keeps the thread asleep, so opening the void never waits for it.

### The dock
- **Window:** a layered, never-activated, topmost window centred in the main monitor's bottom band, kept above the frame and below the void. Clicking it doesn't take the focus from your app, so a click can hand the focus straight to the app you picked.
- **Which apps:** the same test the Windows taskbar uses (visible, unowned or an app window, titled, not cloaked). Windows on other desktops are the ones the shell cloaks. Each window's app ID comes from its property store, or its package for Store-style apps; File Explorer windows group under File Explorer.
- **Staying current:** the dock registers as a shell hook window, so Windows tells it when windows open, close, take the focus, change title or flash. A refresh every 2 seconds catches anything missed.
- **Icons:** the shell's own high-resolution icon for the pin, app ID or exe (`IShellItemImageFactory`), else the window's icon. They're cached at the size they're drawn at.
- **Previews:** an ordinary (not layered) popup, so Windows can draw live thumbnails into it (`DwmRegisterThumbnail`). Windows rounds its corners and draws its crimson border.
- **Starting apps:** a normal Void Shell opens them as Explorer would. When Void Shell runs as administrator it asks Explorer to start them, so apps never inherit administrator rights by accident.
- **Start, search, notifications:** the same keys you'd press (Win, Win+S, Win+N, Win+X).
- **Volume:** the default speakers' master volume (`IAudioEndpointVolume`), re-read 10 times a second while the dock shows.

### Zones
- **Watching drags:** out-of-process window-event hooks see drags start and end. Nothing is injected into other apps.
- **Moving or resizing:** `WM_NCHITTEST` tells a move from a resize. Snapping moves the visible frame into the zone, after correcting for invisible resize borders, and re-checks once in case the app resized itself.
- **Which layout:** each window's zones come from the layout of the desktop it lives on (`IVirtualDesktopManager::GetWindowDesktopId`). Changing a layout re-flows snapped windows.

### Fullscreen detection
Once a second Void Shell checks `SHQueryUserNotificationState`, which reports exclusive-fullscreen Direct3D, "busy" fullscreen apps and presentation mode. It also checks whether the foreground window covers its whole monitor (borderless fullscreen). Desktop and shell windows don't count, and nor does the void itself.

**Desktop API.** Listing, creating and switching desktops uses the shell's undocumented `IVirtualDesktopManagerInternal`. The layout is the one for builds 26100+, checked against [MScholtes/VirtualDesktop](https://github.com/MScholtes/VirtualDesktop) and [Ciantic/VirtualDesktopAccessor](https://github.com/Ciantic/VirtualDesktopAccessor). On a build with a different interface version, the connection fails cleanly and Void Shell falls back to the registry and the Win+Ctrl+D / Win+Ctrl+arrow shortcuts.

| File | What it does |
|---|---|
| `src/main.cpp` | Tray, shortcut, void flows, desktop switching, settings changes, fullscreen detection, veils, render loop |
| `src/settings_window.cpp` | The settings page (layout, painting, hit-testing) and its window |
| `src/settings.cpp` | `void-shell.ini`, per-desktop layouts, shortcut names |
| `src/startup.cpp` | Start at sign-in (Run entry or scheduled task), elevation, restart as administrator |
| `src/frame.cpp` | Shell frame: appbars, painting, top bar buttons, fullscreen handling, the dock's slot |
| `src/dock.cpp` | The dock window: clicks, menus, previews, shell hook, starting apps |
| `src/dock_model.cpp` | Which apps the dock shows: pins, open windows, grouping |
| `src/dock_view.cpp` | The dock and preview layout, painting and hit-testing |
| `src/taskbar.cpp` | Windows' taskbar: auto-hide, its pinned shortcuts, reading shortcuts, pressing keys |
| `src/icons.cpp` | App icons from the shell or a window, app names from exes |
| `src/audio.cpp` | Master volume |
| `src/bubbles.cpp` | Bubble sizes, the sockets in each band, which socket each bubble takes, and how bubbles and sockets are drawn |
| `src/preview.cpp` | The live window preview shared by the dock and the bubbles, and taskbar-style window toggling |
| `src/pickers.cpp` | The app list window, Start's app list, Windows' folder and program pickers |
| `src/sysinfo.cpp` | CPU, GPU and memory use and temperatures, read once a second on a thread of its own |
| `src/stat_orbs.cpp` | Where the system orbs or chips go on the top bar, and how they're drawn |
| `src/zones.cpp` | Zone layouts, drag watching, drag preview, snapping, placing new windows |
| `src/canvas.cpp` | CPU drawing for layered windows: rounded rectangles, glows, lines, text |
| `src/desktops.cpp` | Virtual desktops: shell COM API, registry + shortcut fallback |
| `src/capture.cpp` | Desktop Duplication, GDI fallback, fake desktops for tests |
| `src/wallpaper.cpp` | Wallpapers for unvisited desktops |
| `src/scene.cpp` | Void layout, animation, camera, hover and click picking |
| `src/renderer.cpp` | Direct3D 11: per-desktop, per-monitor pictures and cross-fades, nebula, draw order, labels |
| `src/selftest.cpp` | Headless test modes |
| `shaders/*.hlsl` | Void, desktop morph, planet/plus spheres, halos, text |

## Test modes

```bat
build\void-shell.exe --render-test test-output\render
build\void-shell.exe --render-test test-output\real --capture
build\void-shell.exe --desktops-test test-output\desktops
build\void-shell.exe --frame-test test-output\frame
build\void-shell.exe --settings-test test-output\settings
build\void-shell.exe --startup-test test-output\startup
build\void-shell.exe --dock-test test-output\dock
build\void-shell.exe --sysinfo-test test-output\sysinfo
build\void-shell.exe --bubbles-test test-output\bubbles
```

None of them change your system. Each writes a report into its folder.

- **`--render-test`:** the void's first and last frames match the desktops pixel for pixel. `--capture` uses the real screen and saves no images.
- **`--desktops-test`:** lists desktops through the API, cross-checks them against the registry, and decodes the wallpaper in memory.
- **`--frame-test`:** checks frame geometry, click-through, top-bar buttons (orb, gear, layout) and zones for every layout.
- **`--settings-test`:** every control on the settings page can be clicked; disabled controls don't take clicks; settings and per-desktop layouts survive a save and load (into a test ini). Saves PNGs of the page.
- **`--startup-test`:** Task Scheduler parses the startup task definition without registering it. Also reports the current startup entries.
- **`--dock-test`:** every part of the dock can be hit at two scales; a crowded dock fits; the frame makes room. Renders the dock in the wide margin with a preview above it (`dock_preview.png`, `dock_band.png`, `dock_150.png`). Then reads this PC's taskbar pins, open apps, icons and volume without changing anything, and renders the real dock (`dock_real.png`, icons only). It logs counts, never window titles.

- **`--sysinfo-test`:** where the orbs sit for every frame size (inside the margin, centred, clear of the top bar's buttons), renders over the rain (`orbs_*.png`), then a read-only reading of this PC and a check that the background reader pauses and stops without keeping anyone waiting.

- **`--bubbles-test`:** bubble sizes for every frame, the + button, evenly spaced sockets that skip the orbs and the dock, which socket each bubble takes, where previews open, how Start's app IDs resolve, renders with real icons at rest and mid-drag (`bubbles_*.png`), saving and loading, and the app list's search, keys and clicks (`picker*.png`). Reads Start's app list and which of those apps are open, without changing anything.

Last run (RTX 3070, Windows 11 build 26200): all nine pass.

## Known limits

- Windows' own Snap still runs when you drop a window at a screen edge. Void Shell's zone snap is applied just after it and wins.
- A desktop's ball shows the last picture taken through the void. Desktops changed via Task View show an older picture until you next enter them.
- Turning administrator mode *off* takes effect the next time Void Shell starts normally (an elevated app can't simply drop its rights).
- One zone layout per desktop, used on every monitor.
- HDR desktops are captured as 8-bit.
- The dock lives on the main monitor only. Its pins can't be dragged into a new order yet (unpin and pin again).
- The Start menu, search and notifications open where Windows puts them (above where its taskbar would be), not above the dock.
- With more than about 35 apps on a small screen the dock runs wider than the screen.
- A window that is minimized with nothing to show gets its app's icon in the preview instead of a picture.
- CPU temperature needs an ACPI thermal zone or a running LibreHardwareMonitor / OpenHardwareMonitor (see above). Without one it shows a dash, as it does on most desktops.
- The orbs show the main GPU only (the one with the most video memory).
- Bubbles can't be renamed yet, and dragging a file or folder from Explorer onto the margin doesn't add it yet (use the +).
- A drag can't be cancelled with Esc; drop the bubble back where it was.
- Folder bubbles always open a new Explorer window; they don't yet bring up one that's already showing that folder.

## Roadmap

1. **Phase 1:** hotkey, capture, void, zoom into a sphere and back. *(done)*
2. **Phase 2:** virtual desktops as worlds, a working plus world, a picture per desktop. *(done)*
3. **Phase 3:** outer shell frame (appbars) and zone snapping. *(done)*
4. **Phase 4:** multi-monitor polish, administrator windows, fullscreen detection, settings window, start at sign-in, per-desktop layouts. *(done)*
5. **Phase 5:** remove and reorder desktops, fresher pictures, the wide margin with matrix rain, Venom branding. *(done)*
6. **Phase 6:** the dock, and hiding the Windows taskbar. *(built)*
7. **Phase 7:** system orbs, and the One window zone layout. *(built)*
8. **Phase 8:** folder and app bubbles in the margin. *(built)*
9. **Phase 9 (this):** sockets, taskbar-style app bubbles and their previews. *(built)*

## License

MIT; see [LICENSE](LICENSE).
