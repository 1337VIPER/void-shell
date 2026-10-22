#include "selftest.h"

#include <wincodec.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <climits>
#include <cstdio>
#include <map>
#include <set>

#include "brand.h"
#include "canvas.h"
#include "capture.h"
#include "rain.h"
#include "audio.h"
#include "bubbles.h"
#include "desktops.h"
#include "dock.h"
#include "frame.h"
#include "pickers.h"
#include "renderer.h"
#include "scene.h"
#include "settings.h"
#include "settings_window.h"
#include "startup.h"
#include "stat_orbs.h"
#include "sysinfo.h"
#include "taskbar.h"
#include "wallpaper.h"
#include "zones.h"

using DirectX::XMFLOAT2;

namespace {

GUID FakeDesktopId(int index) {
    return {0x5A1D0000u + unsigned(index), 0x7E57, 0x4D00, {0x80, 0, 0, 0, 0, 0, 0, uint8_t(index)}};
}

std::vector<DesktopInfo> FakeDesktops(int count) {
    std::vector<DesktopInfo> desktops(count);
    for (int i = 0; i < count; ++i) desktops[i].id = FakeDesktopId(i);
    if (count > 1) desktops[1].name = L"Work";
    return desktops;
}

bool SavePng(const std::wstring& path, std::vector<uint8_t> bgra, UINT width, UINT height) {
    for (size_t i = 3; i < bgra.size(); i += 4) bgra[i] = 255;
    ComPtr<IWICImagingFactory> factory;
    CHECK_HR(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)));
    ComPtr<IWICStream> stream;
    CHECK_HR(factory->CreateStream(&stream));
    CHECK_HR(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE));
    ComPtr<IWICBitmapEncoder> encoder;
    CHECK_HR(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder));
    CHECK_HR(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache));
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> properties;
    CHECK_HR(encoder->CreateNewFrame(&frame, &properties));
    CHECK_HR(frame->Initialize(properties.Get()));
    CHECK_HR(frame->SetSize(width, height));
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    CHECK_HR(frame->SetPixelFormat(&format));
    if (format != GUID_WICPixelFormat32bppBGRA) {
        Log("PNG encoder refused 32bpp BGRA");
        return false;
    }
    CHECK_HR(frame->WritePixels(height, width * 4, UINT(bgra.size()), bgra.data()));
    CHECK_HR(frame->Commit());
    CHECK_HR(encoder->Commit());
    return true;
}

bool RenderAndSave(Renderer& renderer, const VoidScene& scene, ID3D11Texture2D* target, UINT width, UINT height,
                   const std::wstring& path) {
    if (!renderer.Render(scene.Frame(), target, width, height)) return false;
    std::vector<uint8_t> pixels;
    UINT w = 0;
    UINT h = 0;
    return renderer.ReadPixels(target, pixels, w, h) && SavePng(path, std::move(pixels), w, h);
}

// Renders the scene and checks the frame equals a desktop picture pixel for pixel.
bool MatchesPicture(Renderer& renderer, const VoidScene& scene, ID3D11Texture2D* picture, ID3D11Texture2D* target,
                    UINT width, UINT height, const char* what) {
    std::vector<uint8_t> expected;
    std::vector<uint8_t> actual;
    UINT w = 0;
    UINT h = 0;
    if (!renderer.Render(scene.Frame(), target, width, height) || !renderer.ReadPixels(picture, expected, w, h) ||
        !renderer.ReadPixels(target, actual, w, h)) {
        Log("%s: could not render or read back", what);
        return false;
    }
    size_t differing = 0;
    int largest = 0;
    for (size_t i = 0, n = size_t(width) * height; i < n; ++i) {
        int diff = 0;
        for (int c = 0; c < 3; ++c) diff = std::max(diff, std::abs(int(expected[i * 4 + c]) - int(actual[i * 4 + c])));
        if (diff) ++differing;
        largest = std::max(largest, diff);
    }
    Log("%s: %zu of %zu pixels differ, largest channel difference %d", what, differing, size_t(width) * height,
        largest);
    return differing == 0;
}

std::wstring Percent(const std::wstring& prefix, float p) {
    return prefix + std::to_wstring(int(p * 100.0f + 0.5f)) + L".png";
}

// A stand-in desktop for the frame test: dusk wallpaper, a taskbar, and windows sitting in the default zones.
void PaintFakeDesktop(Canvas& screen, const std::vector<RECT>& windows, int taskbarHeight) {
    const int width = screen.Width();
    const int height = screen.Height();
    uint32_t* pixels = screen.Pixels();
    const auto pack = [](float r, float g, float b) {
        const auto c = [](float v) { return uint32_t(std::clamp(v, 0.0f, 255.0f)); };
        return 0xFF000000u | (c(r) << 16) | (c(g) << 8) | c(b);
    };
    const auto fill = [&](long x0, long y0, long x1, long y1, uint32_t color) {
        for (long y = std::max(0L, y0); y < std::min(long(height), y1); ++y)
            for (long x = std::max(0L, x0); x < std::min(long(width), x1); ++x) pixels[size_t(y) * width + x] = color;
    };
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const float fx = float(x) / width;
            const float fy = float(y) / height;
            const float glow = expf(-((fx - 0.7f) * (fx - 0.7f) * 7.0f + (fy - 0.6f) * (fy - 0.6f) * 9.0f));
            pixels[size_t(y) * width + x] = pack(18 + 70 * fy + 150 * glow, 26 + 30 * fy + 80 * glow, 60 + 70 * (1 - fy) + 90 * glow);
        }
    }
    fill(0, height - taskbarHeight, width, height, pack(28, 30, 38));
    const uint32_t bodies[] = {pack(244, 245, 248), pack(30, 32, 40)};
    const uint32_t lines[] = {pack(190, 194, 204), pack(90, 140, 200)};
    for (size_t i = 0; i < windows.size(); ++i) {
        const RECT& w = windows[i];
        const int style = int(i % 2);
        fill(w.left, w.top, w.right, w.top + 32, style ? pack(44, 46, 56) : pack(222, 225, 232));
        fill(w.left, w.top + 32, w.right, w.bottom, bodies[style]);
        for (long y = w.top + 60; y + 8 < w.bottom - 20; y += 26)
            fill(w.left + 24, y, w.left + 24 + (w.right - w.left - 48) * (55 + (y * 7) % 40) / 100, y + 8, lines[style]);
    }
}

bool SaveCrop(Canvas& canvas, int x, int y, int width, int height, const std::wstring& path) {
    std::vector<uint8_t> bytes(size_t(width) * height * 4);
    for (int row = 0; row < height; ++row)
        memcpy(&bytes[size_t(row) * width * 4], canvas.Pixels() + size_t(y + row) * canvas.Width() + x, size_t(width) * 4);
    return SavePng(path, std::move(bytes), UINT(width), UINT(height));
}

int Run(const std::wstring& outDir, bool useCapture) {
    const HMONITOR monitor = MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
    const ComPtr<IDXGIAdapter1> adapter = FindAdapterForMonitor(monitor);
    Renderer renderer;
    if (!renderer.Init(adapter.Get())) {
        Log("FAIL: renderer did not initialise");
        return 1;
    }
    Log("Adapter: %s, %ux MSAA", renderer.AdapterName(), renderer.SampleCount());

    // Three fake desktops with the middle one current, so the focused world is not at the layout's centre.
    const std::vector<DesktopInfo> desktops = FakeDesktops(3);
    const GUID current = desktops[1].id;

    ComPtr<ID3D11Texture2D> currentPicture;
    if (useCapture) {
        const char* method = "none";
        if (!CaptureMonitor(renderer.Device(), renderer.Context(), monitor, currentPicture, &method)) {
            Log("FAIL: could not capture the primary monitor");
            return 2;
        }
        Log("Capture: primary monitor via %s", method);
    } else if (!CreateTestPattern(renderer.Device(), 1920, 1080, 0, currentPicture)) {
        Log("FAIL: could not build the test pattern");
        return 2;
    }
    D3D11_TEXTURE2D_DESC pictureDesc;
    currentPicture->GetDesc(&pictureDesc);
    const UINT width = pictureDesc.Width;
    const UINT height = pictureDesc.Height;
    Log("Current desktop picture: %ux%u (%s)", width, height, useCapture ? "real screen" : "fake desktop");

    // Desktop 0 gets a different fake picture; desktop 2 has none yet and shows the empty-world gradient.
    ComPtr<ID3D11Texture2D> otherPicture;
    if (!renderer.StoreSnapshot(current, currentPicture.Get(), false) ||
        !CreateTestPattern(renderer.Device(), width, height, 1, otherPicture) ||
        !renderer.StoreSnapshot(desktops[0].id, otherPicture.Get(), false)) {
        Log("FAIL: could not store desktop pictures");
        return 3;
    }

    D3D11_TEXTURE2D_DESC targetDesc = pictureDesc;
    targetDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    targetDesc.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> target;
    if (FAILED(renderer.Device()->CreateTexture2D(&targetDesc, nullptr, &target))) {
        Log("FAIL: could not create the offscreen target");
        return 3;
    }

    VoidScene scene;
    scene.SetAspect(float(width) / float(height));
    scene.SetDesktops(desktops, current, true);
    scene.Open();
    bool pass = true;

    // 1. Zooming out must start exactly on the current desktop, or the overlay would visibly "pop".
    scene.SetProgressForTest(0.0f);
    pass &= MatchesPicture(renderer, scene, currentPicture.Get(), target.Get(), width, height,
                           "Zoom out, first frame vs current desktop");

    // 2. Frames of the animation. Only with fake desktops, so real screen contents never reach disk.
    if (!useCapture) {
        for (float p : {0.0f, 0.15f, 0.3f, 0.45f, 0.6f, 0.8f, 1.0f}) {
            scene.SetProgressForTest(p);
            RenderAndSave(renderer, scene, target.Get(), width, height, Percent(outDir + L"\\frame_", p));
        }
        scene.SetHoverForTest(2, 0.0f, 0.0f);
        RenderAndSave(renderer, scene, target.Get(), width, height, outDir + L"\\hover_world.png");
        scene.SetHoverForTest(-1, 1.0f, 0.6f);
        RenderAndSave(renderer, scene, target.Get(), width, height, outDir + L"\\hover_plus.png");
        scene.SetHoverForTest(-1, 0.0f, 0.0f);

        // Removing: the badge on a hovered world, then armed after the first click.
        scene.SetCanRearrange(true);
        scene.SetHoverForTest(2, 0.0f, 0.0f);
        scene.SetBadgeForTest(2, false);
        RenderAndSave(renderer, scene, target.Get(), width, height, outDir + L"\\badge.png");
        scene.ArmRemove(2, L"Click × again to remove Desktop 3  ·  its windows move to Work");
        RenderAndSave(renderer, scene, target.Get(), width, height, outDir + L"\\badge_armed.png");
        scene.SetBadgeForTest(-1, false);
        scene.SetHoverForTest(-1, 0.0f, 0.0f);

        // Reordering: press on the first world and drag it over the third.
        const FrameDesc layout = scene.Frame();
        const Camera camera = BuildCamera(layout, float(width) / float(height));
        XMFLOAT2 from, to;
        ProjectToScreen(camera, WorldCenter(layout.worlds[0]), width, height, from);
        ProjectToScreen(camera, WorldCenter(layout.worlds[2]), width, height, to);
        scene.PointerDown(from.x, from.y, width, height);
        for (int step = 1; step <= 30; ++step) {
            const float t = step / 30.0f;
            scene.Update(1.0f / 60.0f, from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t, width, height);
        }
        const bool dragging = scene.Dragging();
        RenderAndSave(renderer, scene, target.Get(), width, height, outDir + L"\\drag.png");
        const SceneAction drop = scene.PointerUp(to.x, to.y, width, height);
        const bool reordered = dragging && drop.type == SceneAction::Type::Reorder && drop.index == 0 && drop.to == 2;
        Log("%s  Dragging the first world onto the third reorders it (%d -> %d)", reordered ? "ok  " : "FAIL",
            drop.index + 1, drop.to + 1);
        pass = pass && reordered;
        scene.SetDesktops(desktops, current, true);  // back to the original order for the rest of the test
        scene.SetProgressForTest(1.0f);
    }

    // 3. Zooming into another world must end exactly on that desktop's picture.
    scene.SetProgressForTest(1.0f);
    scene.Enter(0, false);
    if (!useCapture) {
        for (float p : {0.7f, 0.4f, 0.15f}) {
            scene.SetProgressForTest(p);
            RenderAndSave(renderer, scene, target.Get(), width, height, Percent(outDir + L"\\enter_", p));
        }
    }
    scene.SetProgressForTest(0.0f);
    pass &= MatchesPicture(renderer, scene, otherPicture.Get(), target.Get(), width, height,
                           "Zoom into another desktop, last frame vs its picture");

    // 4. Layout: a new desktop popping out of the plus world, the settled four, and eight in two rows.
    scene.Hide();
    scene.SetDesktops(desktops, current, true);
    scene.Open();
    scene.SetProgressForTest(1.0f);
    if (!useCapture) {
        scene.SetDesktops(FakeDesktops(4), current, false);
        for (int i = 0; i < 4; ++i) scene.Update(0.05f, -100.0f, -100.0f, width, height);
        RenderAndSave(renderer, scene, target.Get(), width, height, outDir + L"\\birth.png");
        for (int i = 0; i < 40; ++i) scene.Update(0.05f, -100.0f, -100.0f, width, height);
        RenderAndSave(renderer, scene, target.Get(), width, height, outDir + L"\\four.png");
        scene.SetDesktops(FakeDesktops(8), current, true);
        RenderAndSave(renderer, scene, target.Get(), width, height, outDir + L"\\eight.png");
        scene.SetDesktops(desktops, current, true);
        Log("Saved animation frames to the output folder");
    }

    // 5. Rough GPU cost of a fully open frame with three worlds.
    ComPtr<ID3D11Query> query;
    const D3D11_QUERY_DESC queryDesc = {D3D11_QUERY_EVENT, 0};
    if (SUCCEEDED(renderer.Device()->CreateQuery(&queryDesc, &query))) {
        const auto waitForGpu = [&] {
            renderer.Context()->End(query.Get());
            while (renderer.Context()->GetData(query.Get(), nullptr, 0, 0) == S_FALSE) {
            }
        };
        renderer.Render(scene.Frame(), target.Get(), width, height);
        waitForGpu();
        LARGE_INTEGER frequency, start, end;
        QueryPerformanceFrequency(&frequency);
        QueryPerformanceCounter(&start);
        const int frames = 60;
        for (int i = 0; i < frames; ++i) renderer.Render(scene.Frame(), target.Get(), width, height);
        waitForGpu();
        QueryPerformanceCounter(&end);
        const double ms = double(end.QuadPart - start.QuadPart) * 1000.0 / double(frequency.QuadPart) / frames;
        Log("Open-void frame cost: %.2f ms at %ux%u with 3 worlds", ms, width, height);
    }

    Log(pass ? "RESULT: PASS" : "RESULT: FAIL (a first or last frame does not match its desktop)");
    return pass ? 0 : 4;
}

}  // namespace

int RunRenderTest(const std::wstring& outDir, bool useCapture) {
    CreateDirectoryW(outDir.c_str(), nullptr);
    LogOpen((outDir + L"\\report.txt").c_str());
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const int result = Run(outDir, useCapture);
    if (SUCCEEDED(hr)) CoUninitialize();
    return result;
}

int RunFrameTest(const std::wstring& outDir) {
    CreateDirectoryW(outDir.c_str(), nullptr);
    LogOpen((outDir + L"\\frame.txt").c_str());
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool pass = true;
    const auto check = [&](bool ok, const char* what) {
        Log("%s  %s", ok ? "ok  " : "FAIL", what);
        pass = pass && ok;
    };

    // A 2560x1440 monitor at 100% with a 48 px taskbar along the bottom, and the opening the shell would leave
    // once the four bands are reserved.
    const RECT monitor = {0, 0, 2560, 1440};
    const int taskbar = 48;
    FrameGeometry g = FrameSizes(monitor, 1.0f, FrameStyle{});  // slim first
    g.inner = {monitor.left + g.side, monitor.top + g.top, monitor.right - g.side, monitor.bottom - taskbar - g.bottom};
    Log("Bands: top %d, sides %d, bottom %d, corner radius %d; desktop opening %ldx%ld", g.top, g.side, g.bottom,
        g.radius, g.inner.right - g.inner.left, g.inner.bottom - g.inner.top);

    RECT bars[4];
    FrameBarRects(g, bars);
    check(bars[0].left == monitor.left && bars[0].right == monitor.right && bars[0].top == monitor.top,
          "top bar spans the monitor's top edge");
    check(bars[1].top == bars[0].bottom && bars[1].bottom == bars[3].top && bars[2].top == bars[0].bottom &&
              bars[2].bottom == bars[3].top,
          "side bars meet the top and bottom bars with no gap or overlap");
    check(bars[3].bottom == monitor.bottom - taskbar, "bottom bar sits on top of the taskbar");

    const ZoneLayout& layout = ZoneLayouts()[kDefaultLayout];
    const TopBarContent content = {L"VenomMind", L"Desktop 1", &layout};
    const std::vector<RECT> zones = ComputeZones(layout, g.inner, 1.0f);
    Canvas screen;
    screen.Resize(monitor.right - monitor.left, monitor.bottom - monitor.top);
    PaintFakeDesktop(screen, {zones[0], zones[1]}, taskbar);

    Canvas bar;
    for (int i = 0; i < 4; ++i) {
        bar.Resize(bars[i].right - bars[i].left, bars[i].bottom - bars[i].top);
        PaintFrameBar(bar, g, bars[i], i == 0, content, i == 0 ? FrameButton::Void : FrameButton::None);
        screen.Draw(bar, bars[i].left, bars[i].top);
    }

    // The top bar's overhang over the desktop must be see-through (so clicks pass) except at the rounded corners.
    const int topWidth = bars[0].right - bars[0].left;
    bar.Resize(topWidth, bars[0].bottom - bars[0].top);
    PaintFrameBar(bar, g, bars[0], true, content, FrameButton::None);
    const auto alphaAt = [&](int x, int y) { return bar.Pixels()[size_t(y) * bar.Width() + x] >> 24; };
    check(alphaAt(topWidth / 2, g.top + g.radius / 2) == 0, "overhang is fully transparent away from the corners");
    check(alphaAt(g.side + 1, g.top + 1) > 200, "rounded corner fills the inside corner of the opening");
    check(alphaAt(topWidth / 2, g.top / 2) > 200, "the band itself is solid");

    check(FrameButtonAt(g, topWidth, {g.side + 20, g.top / 2}) == FrameButton::Void, "the orb opens the void");
    check(FrameButtonAt(g, topWidth, {topWidth - g.side - 30, g.top / 2}) == FrameButton::Layout,
          "the layout button is on the right");
    check(FrameButtonAt(g, topWidth, {topWidth / 2, g.top / 2}) == FrameButton::None, "the middle of the bar is not a button");
    int gearAt = -1;
    int layoutAt = -1;
    for (int x = topWidth - 1; x >= topWidth / 2; --x) {
        const FrameButton button = FrameButtonAt(g, topWidth, {x, g.top / 2});
        if (button == FrameButton::Layout && layoutAt < 0) layoutAt = x;
        if (button == FrameButton::Settings && gearAt < 0) gearAt = x;
    }
    check(gearAt > 0 && gearAt < layoutAt, "the settings gear sits just left of the layout button");

    SavePng(outDir + L"\\frame.png", screen.Bytes(), UINT(screen.Width()), UINT(screen.Height()));
    SaveCrop(screen, 0, 0, 520, 140, outDir + L"\\frame_top_left.png");
    SaveCrop(screen, screen.Width() - 520, 0, 520, 140, outDir + L"\\frame_top_right.png");
    SaveCrop(screen, 0, screen.Height() - 160, 520, 160, outDir + L"\\frame_bottom_left.png");

    // Every layout's zones sit inside the opening, never overlap, and keep at least the 10 px gap.
    for (const ZoneLayout& candidate : ZoneLayouts()) {
        const std::vector<RECT> z = ComputeZones(candidate, g.inner, 1.0f);
        bool inside = true;
        bool apart = true;
        long smallestGap = LONG_MAX;
        for (size_t i = 0; i < z.size(); ++i) {
            inside = inside && z[i].left >= g.inner.left && z[i].top >= g.inner.top && z[i].right <= g.inner.right &&
                     z[i].bottom <= g.inner.bottom && z[i].right > z[i].left && z[i].bottom > z[i].top;
            for (size_t j = i + 1; j < z.size(); ++j) {
                const RECT& a = z[i];
                const RECT& b = z[j];
                const bool rows = a.top < b.bottom && b.top < a.bottom;
                const bool columns = a.left < b.right && b.left < a.right;
                if (rows && columns) apart = false;
                if (rows) smallestGap = std::min(smallestGap, std::max(b.left - a.right, a.left - b.right));
                if (columns) smallestGap = std::min(smallestGap, std::max(b.top - a.bottom, a.top - b.bottom));
            }
        }
        const bool ok = inside && apart && smallestGap >= 10;
        const std::string gap = smallestGap == LONG_MAX ? "no neighbours" : "smallest gap " + std::to_string(smallestGap) + " px";
        Log("%s  %ls: %zu zone%s, %s%s%s", ok ? "ok  " : "FAIL", candidate.name, z.size(), z.size() == 1 ? "" : "s",
            gap.c_str(), inside ? "" : ", NOT inside the opening", apart ? "" : ", OVERLAPPING");
        pass = pass && ok;
    }
    check(ZoneAt(zones, {(zones[0].right + zones[1].left) / 2, (zones[1].top + zones[1].bottom) / 2}, 1.0f) >= 0,
          "dropping in the gap between zones picks a neighbour");
    check(ZoneAt(zones, {g.inner.left + 5, 2000}, 1.0f) == -1, "dropping far outside the work area snaps nowhere");

    // The drag preview over the desktop, with the top-right zone lit.
    Canvas preview;
    preview.Resize(g.inner.right - g.inner.left, g.inner.bottom - g.inner.top);
    std::vector<RECT> local = zones;
    for (RECT& r : local) OffsetRect(&r, -g.inner.left, -g.inner.top);
    DrawZonePreview(preview, local, 1, 1.0f);
    screen.Draw(preview, g.inner.left, g.inner.top);
    SavePng(outDir + L"\\zones.png", screen.Bytes(), UINT(screen.Width()), UINT(screen.Height()));

    // The wide margin with matrix rain, as in the sketch: a broad frame on every side with code falling inward.
    FrameStyle wideStyle;
    wideStyle.wide = true;
    wideStyle.margin = 1;
    FrameGeometry w = FrameSizes(monitor, 1.0f, wideStyle);
    w.inner = {monitor.left + w.side, monitor.top + w.top, monitor.right - w.side, monitor.bottom - taskbar - w.bottom};
    Log("Wide bands: top %d (strip %d), sides %d, bottom %d; desktop opening %ldx%ld", w.top, w.strip, w.side, w.bottom,
        w.inner.right - w.inner.left, w.inner.bottom - w.inner.top);
    RECT wideBars[4];
    FrameBarRects(w, wideBars);
    const std::vector<RECT> wideZones = ComputeZones(ZoneLayouts()[4], w.inner, 1.0f);  // the sketch's 2x2 grid
    PaintFakeDesktop(screen, wideZones, taskbar);

    RainGlyphs glyphs;
    glyphs.Build(13, 16);
    check(glyphs.Count() > 50, "rain glyphs rendered (mirrored katakana, digits, symbols)");
    Canvas stills[4];
    Canvas shown[4];
    RainField rain[4];
    for (int i = 0; i < 4; ++i) {
        stills[i].Resize(wideBars[i].right - wideBars[i].left, wideBars[i].bottom - wideBars[i].top);
        shown[i].Resize(stills[i].Width(), stills[i].Height());
        PaintFrameBar(stills[i], w, wideBars[i], i == 0, content, i == 0 ? FrameButton::Settings : FrameButton::None);
        RECT area;
        RainFlow flow;
        if (FrameRainArea(w, wideBars[i], i, area, flow)) rain[i].Reset(area, flow, glyphs, 1234u + unsigned(i));
        for (int step = 0; step < 40; ++step) rain[i].Step(0.05f);  // two seconds in, so drops are mid-fall
    }

    // Rain cost per frame at 20 frames a second (all four bars: restore the still image, step, draw).
    LARGE_INTEGER frequency, start, end;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&start);
    const int frames = 40;
    for (int f = 0; f < frames; ++f) {
        for (int i = 0; i < 4; ++i) {
            shown[i].CopyFrom(stills[i]);
            rain[i].Step(0.05f);
            rain[i].Draw(shown[i], glyphs, RainColor::Venom);
        }
    }
    QueryPerformanceCounter(&end);
    const double rainMs = double(end.QuadPart - start.QuadPart) * 1000.0 / double(frequency.QuadPart) / frames;
    Log("Rain frame cost: %.2f ms for all four bars (about %.0f%% of one core at 20 fps)", rainMs, rainMs * 2.0);
    check(rainMs < 15.0, "rain is cheap enough to run all the time");

    size_t changed = 0;
    for (int i = 0; i < 4; ++i) {
        for (int p = 0; p < shown[i].Width() * shown[i].Height(); ++p) changed += shown[i].Pixels()[p] != stills[i].Pixels()[p];
        screen.Draw(shown[i], wideBars[i].left, wideBars[i].top);
    }
    check(changed > 10000, "code is falling in the margin");
    const int wideTop = wideBars[0].right - wideBars[0].left;
    const auto shownAlpha = [&](int x, int y) { return shown[0].Pixels()[size_t(y) * shown[0].Width() + x] >> 24; };
    check(shownAlpha(wideTop / 2, w.top + w.radius / 2) == 0, "rain never covers the desktop (overhang stays clear)");
    check(FrameButtonAt(w, wideTop, {w.side + 20, w.top - w.strip / 2}) == FrameButton::Void,
          "the strip's mark opens the void in the wide style too");
    SavePng(outDir + L"\\frame_wide.png", screen.Bytes(), UINT(screen.Width()), UINT(screen.Height()));
    SaveCrop(screen, 0, 0, 700, 300, outDir + L"\\frame_wide_top_left.png");
    SaveCrop(screen, screen.Width() - 700, 0, 700, 300, outDir + L"\\frame_wide_top_right.png");

    // The Venom mark, plain (as icon.svg) and glowing (as the social-kit avatar), large and at tray size.
    Canvas marks;
    marks.Resize(360, 140);
    marks.FillRoundRect(0, 0, 360, 140, 0.0f, venom::kVoid);
    venom::DrawMark(marks, 70, 70, 110, venom::MarkStyle::Plain);
    venom::DrawMark(marks, 200, 70, 110, venom::MarkStyle::Glow, true);
    venom::DrawMark(marks, 300, 50, 32, venom::MarkStyle::Glow);
    venom::DrawMark(marks, 300, 100, 16, venom::MarkStyle::Glow);
    SavePng(outDir + L"\\mark.png", marks.Bytes(), 360, 140);
    Log("Fonts: display %ls, interface %ls, mono %ls, rain %ls", venom::DisplayFont(), venom::UiFont(),
        venom::MonoFont(), venom::RainFont());

    Log(pass ? "RESULT: PASS" : "RESULT: FAIL");
    if (SUCCEEDED(hr)) CoUninitialize();
    return pass ? 0 : 5;
}

int RunSettingsTest(const std::wstring& outDir) {
    CreateDirectoryW(outDir.c_str(), nullptr);
    LogOpen((outDir + L"\\settings.txt").c_str());
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool pass = true;
    const auto check = [&](bool ok, const char* what) {
        Log("%s  %s", ok ? "ok  " : "FAIL", what);
        pass = pass && ok;
    };

    Settings settings;
    settings.runAsAdmin = true;  // shows the restart button
    SettingsContext context;
    context.desktopName = L"Work";
    context.desktopLayout = 4;
    context.startupNote = L"Restart as administrator to finish: it starts without admin rights until then.";

    // Every control must be reachable: sweep the page and collect what each point hits.
    const auto sweep = [](const SettingsView& view, std::map<SettingId, std::set<int>>& hits) {
        for (LONG y = 0; y < view.Height(); y += 3) {
            for (LONG x = 0; x < view.Width(); x += 3) {
                int part = -1;
                const SettingId id = view.HitTest({x, y}, &part);
                if (id != SettingId::None) hits[id].insert(part);
            }
        }
    };

    // Sweep every tab; each tab is the same size so the window doesn't jump.
    SettingsView view;
    std::map<SettingId, std::set<int>> hits;
    int height = -1;
    bool sameSize = true;
    Canvas canvas;
    const SettingId hovers[kSettingsTabs] = {SettingId::Hotkey, SettingId::FrameStyle, SettingId::DockDesktops,
                                             SettingId::Snap, SettingId::RestartElevated};
    for (int tab = 0; tab < kSettingsTabs; ++tab) {
        view.Build(settings, context, 1.0f, tab);
        sweep(view, hits);
        sameSize = sameSize && (height < 0 || height == view.Height());
        height = view.Height();
        canvas.Resize(view.Width(), view.Height());
        view.Paint(canvas, hovers[tab], tab == 1 || tab == 2 ? 1 : -1, false, L"");
        SavePng(outDir + L"\\settings_tab" + std::to_wstring(tab + 1) + L".png", canvas.Bytes(), UINT(canvas.Width()),
                UINT(canvas.Height()));
    }
    const struct {
        SettingId id;
        const char* name;
    } controls[] = {
        {SettingId::Close, "close"}, {SettingId::Hotkey, "shortcut"}, {SettingId::PauseInFullscreen, "fullscreen pause"},
        {SettingId::Frame, "frame"}, {SettingId::FrameStyle, "frame style"}, {SettingId::FrameMargin, "margin size"},
        {SettingId::Rain, "matrix rain"}, {SettingId::RainColor, "rain colour"}, {SettingId::SystemStats, "system stats"},
        {SettingId::ShowSockets, "show sockets"},
        {SettingId::FrameMonitors, "frame monitors"}, {SettingId::Dock, "dock"},
        {SettingId::DockDesktops, "dock desktops"}, {SettingId::HideTaskbar, "hide the Windows taskbar"},
        {SettingId::ResetPins, "pin taskbar apps again"}, {SettingId::Snap, "snap"},
        {SettingId::AutoPlace, "place new windows"}, {SettingId::DesktopLayout, "this desktop's layout"},
        {SettingId::DefaultLayout, "default layout"}, {SettingId::ResetLayouts, "reset layouts"},
        {SettingId::StartAtLogin, "start at sign-in"}, {SettingId::RunAsAdmin, "run as administrator"},
        {SettingId::RestartElevated, "restart as administrator"}, {SettingId::OpenLog, "open log"},
        {SettingId::OpenFolder, "open folder"},
    };
    bool allReachable = true;
    for (const auto& control : controls) {
        if (!hits.count(control.id)) {
            Log("FAIL  %s can't be clicked", control.name);
            allReachable = false;
        }
    }
    check(allReachable, "every control can be clicked on its tab");
    check(hits[SettingId::Tab].size() == kSettingsTabs, "all five tabs can be picked");
    check(sameSize, "every tab is the same size");
    check(hits[SettingId::DesktopLayout].size() == ZoneLayouts().size() &&
              hits[SettingId::DefaultLayout].size() == ZoneLayouts().size(),
          "every layout card can be picked, for this desktop and the default");
    check(hits[SettingId::FrameMonitors].size() == 2 && hits[SettingId::FrameMargin].size() == 3 &&
              hits[SettingId::RainColor].size() == 3 && hits[SettingId::DockDesktops].size() == 2,
          "every choice segment can be picked");
    check(!view.InTitleBar({20, view.Height() - 5}) && view.InTitleBar({200, 20}), "only the title bar drags the window");
    Log("Page size at 100%%: %dx%d", view.Width(), view.Height());

    // Frame off greys out everything that depends on it; slim style greys out the margin and rain.
    // Painted at 150% while recording a shortcut.
    settings.frame = false;
    settings.runAsAdmin = false;
    context.startupNote.clear();
    hits.clear();
    for (int tab = 0; tab < kSettingsTabs; ++tab) {
        view.Build(settings, context, 1.5f, tab);
        sweep(view, hits);
    }
    check(!hits.count(SettingId::FrameMonitors) && !hits.count(SettingId::FrameStyle) && !hits.count(SettingId::Rain),
          "frame options are disabled while the frame is off");
    check(!hits.count(SettingId::Dock) && !hits.count(SettingId::DockDesktops) && !hits.count(SettingId::HideTaskbar) &&
              !hits.count(SettingId::ResetPins) && !hits.count(SettingId::SystemStats) &&
              !hits.count(SettingId::ShowSockets),
          "the dock's options are disabled while the frame is off (it lives in the frame)");
    check(!hits.count(SettingId::RestartElevated), "no restart button unless administrator mode is wanted");
    settings.frame = true;
    settings.frameWide = false;
    hits.clear();
    view.Build(settings, context, 1.0f, 1);
    sweep(view, hits);
    check(hits.count(SettingId::FrameStyle) && !hits.count(SettingId::FrameMargin) && !hits.count(SettingId::Rain),
          "margin and rain are disabled in the slim style");
    view.Build(settings, context, 1.5f, 0);
    canvas.Resize(view.Width(), view.Height());
    view.Paint(canvas, SettingId::None, -1, true, L"");
    SavePng(outDir + L"\\settings_recording_150.png", canvas.Bytes(), UINT(canvas.Width()), UINT(canvas.Height()));

    // Settings survive a save and load (into a test file, not the real one).
    Settings original;
    original.hotkeyModifiers = MOD_WIN | MOD_SHIFT;
    original.hotkeyKey = 'V';
    original.frameAllMonitors = false;
    original.pauseInFullscreen = false;
    original.defaultLayout = 4;
    original.frameWide = false;
    original.frameMargin = 2;
    original.matrixRain = false;
    original.rainColor = 1;
    original.brandName = L"Test Brand";
    original.dock = false;
    original.systemStats = false;
    original.showSockets = false;
    original.dockAllDesktops = true;
    original.hideWindowsTaskbar = true;
    original.taskbarRestore = 0;
    original.dockSeeded = true;
    original.dockPins = {L"C:\\Test\\App.lnk", L"shell:AppsFolder\\Test.App_123!App", L"C:\\Test\\tool.exe"};
    const GUID desktop = {0x12345678, 0x1234, 0x5678, {1, 2, 3, 4, 5, 6, 7, 8}};
    original.SetLayoutFor(desktop, 1);
    const std::wstring ini = outDir + L"\\roundtrip.ini";
    DeleteFileW(ini.c_str());
    original.Save(ini);
    Settings loaded;
    loaded.Load(ini);
    check(loaded.hotkeyModifiers == original.hotkeyModifiers && loaded.hotkeyKey == original.hotkeyKey &&
              loaded.frameAllMonitors == original.frameAllMonitors && loaded.pauseInFullscreen == original.pauseInFullscreen &&
              loaded.defaultLayout == 4 && loaded.LayoutFor(desktop) == 1 && loaded.LayoutFor(GUID{}) == 4 &&
              loaded.frameWide == original.frameWide && loaded.frameMargin == 2 && !loaded.matrixRain &&
              loaded.rainColor == 1 && loaded.brandName == L"Test Brand",
          "settings, including per-desktop layouts, frame style and brand, survive save and load");
    check(!loaded.dock && !loaded.systemStats && !loaded.showSockets && loaded.dockAllDesktops && loaded.hideWindowsTaskbar && loaded.taskbarRestore == 0 &&
              loaded.dockSeeded && loaded.dockPins == original.dockPins,
          "dock settings and pins, in order, survive save and load");
    Settings fresh;
    fresh.Load(outDir + L"\\missing.ini");
    check(fresh.dock && !fresh.hideWindowsTaskbar && fresh.taskbarRestore == -1 && !fresh.dockSeeded && fresh.dockPins.empty(),
          "a new install shows the dock, leaves the Windows taskbar alone and seeds pins once");
    // An ini from before "One window" existed: its layout numbers still mean the same layouts.
    const std::wstring oldIni = outDir + L"\\old_layouts.ini";
    DeleteFileW(oldIni.c_str());
    WritePrivateProfileStringW(L"VoidShell", L"DefaultLayout", L"2", oldIni.c_str());  // was Main and side
    WritePrivateProfileStringW(L"DesktopLayouts", GuidToString(desktop).c_str(), L"0", oldIni.c_str());  // was Two columns
    Settings migrated;
    migrated.Load(oldIni);
    check(ZoneLayouts()[migrated.defaultLayout].name == std::wstring(L"Main and side") &&
              ZoneLayouts()[migrated.LayoutFor(desktop)].name == std::wstring(L"Two columns"),
          "layouts saved before One window was added still open as the same layouts");
    check(ZoneLayouts()[0].zones.size() == 1 && ZoneLayouts()[kDefaultLayout].name == std::wstring(L"Main and side"),
          "One window is a single zone, first in the list; the default is still Main and side");
    Log("Shortcut text: \"%ls\" and \"%ls\"", HotkeyText(MOD_CONTROL | MOD_ALT, VK_SPACE).c_str(),
        HotkeyText(original.hotkeyModifiers, original.hotkeyKey).c_str());

    Log(pass ? "RESULT: PASS" : "RESULT: FAIL");
    if (SUCCEEDED(hr)) CoUninitialize();
    return pass ? 0 : 6;
}

int RunStartupTest(const std::wstring& outDir) {
    CreateDirectoryW(outDir.c_str(), nullptr);
    LogOpen((outDir + L"\\startup.txt").c_str());
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const std::wstring user = CurrentUserName();
    Log("Running as administrator: %s; user %ls", IsElevated() ? "yes" : "no", user.c_str());

    const std::wstring xml = StartupTaskXml(ExePath(), user);
    std::wstring error;
    const bool valid = ValidateTaskXml(xml, error);
    Log("%s  Task Scheduler %s the startup task definition%s%ls", valid ? "ok  " : "FAIL",
        valid ? "accepts" : "rejects", valid ? "" : ": ", error.c_str());
    FILE* file = nullptr;
    if (_wfopen_s(&file, (outDir + L"\\task.xml").c_str(), L"w, ccs=UTF-8") == 0 && file) {
        fputws(xml.c_str(), file);
        fclose(file);
    }

    const StartupStatus status = ReadStartup();
    Log("Current startup entries: Run entry %s, scheduled task %s", status.runEntry ? "present" : "none",
        status.task ? "present" : "none");
    Log(valid ? "RESULT: PASS" : "RESULT: FAIL");
    if (SUCCEEDED(hr)) CoUninitialize();
    return valid ? 0 : 7;
}

int RunDesktopsTest(const std::wstring& outDir) {
    CreateDirectoryW(outDir.c_str(), nullptr);
    LogOpen((outDir + L"\\desktops.txt").c_str());
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int result = 0;
    {
        VirtualDesktops desktops;
        desktops.Init();
        Log("Mode: %s", desktops.Mode());
        std::vector<DesktopInfo> list;
        GUID current = {};
        if (!desktops.List(list, current)) {
            Log("FAIL: could not list desktops");
            result = 1;
        } else {
            int currentIndex = -1;
            for (size_t i = 0; i < list.size(); ++i) {
                if (list[i].id == current) currentIndex = int(i);
                Log("  %zu. %ls  name \"%ls\"  wallpaper: %s", i + 1, GuidToString(list[i].id).c_str(),
                    list[i].name.c_str(), list[i].wallpaper.empty() ? "global" : "its own");
            }
            Log("Desktops: %zu, current is number %d", list.size(), currentIndex + 1);

            // The registry is a second opinion: with the COM API, disagreement would point at a wrong layout.
            std::vector<DesktopInfo> registry;
            GUID registryCurrent = {};
            const bool haveRegistry = VirtualDesktops::ListFromRegistry(registry, registryCurrent);
            bool agree = haveRegistry && registry.size() == list.size() && registryCurrent == current;
            for (size_t i = 0; agree && i < list.size(); ++i) agree = registry[i].id == list[i].id;
            if (!haveRegistry) {
                Log("Registry has no desktop list yet (normal until a second desktop has existed)");
            } else {
                Log("Registry %s the list", agree ? "agrees with" : "does NOT agree with");
            }

            GUID again = {};
            const bool currentOk = desktops.Current(again) && again == current;
            Log("Current-desktop query: %s", currentOk ? "ok" : "mismatch");
            if (currentIndex < 0 || !currentOk) result = 2;
            if (strcmp(desktops.Mode(), "shell COM API") == 0 && haveRegistry && !agree) result = 3;
        }

        // Unvisited worlds show the wallpaper; decode it in memory only (nothing is saved).
        const std::wstring wallpaper = GlobalWallpaperPath();
        std::vector<uint8_t> pixels;
        LARGE_INTEGER frequency, start, end;
        QueryPerformanceFrequency(&frequency);
        QueryPerformanceCounter(&start);
        const bool decoded = LoadImageFill(wallpaper, 1280, 720, pixels);
        QueryPerformanceCounter(&end);
        Log("Wallpaper: %s, %s (%.0f ms)", wallpaper.empty() ? "none set (solid colour)" : "file present",
            decoded ? "decoded" : "not decodable, worlds will use the desktop colour",
            double(end.QuadPart - start.QuadPart) * 1000.0 / double(frequency.QuadPart));
    }
    Log(result == 0 ? "RESULT: PASS" : "RESULT: FAIL");
    if (SUCCEEDED(hr)) CoUninitialize();
    return result;
}

namespace {

// A stand-in app icon for the render: a rounded tile in a colour, with a lighter glyph bar, premultiplied.
IconImage FakeIcon(int size, Rgba color) {
    Canvas canvas;
    canvas.Resize(size, size);
    const float s = float(size);
    canvas.FillRoundRect(0, 0, s, s, s * 0.22f, color);
    canvas.FillRoundRect(s * 0.22f, s * 0.30f, s * 0.78f, s * 0.42f, s * 0.06f, {1.0f, 1.0f, 1.0f, 0.85f});
    canvas.FillRoundRect(s * 0.22f, s * 0.54f, s * 0.60f, s * 0.66f, s * 0.06f, {1.0f, 1.0f, 1.0f, 0.6f});
    IconImage icon;
    icon.size = size;
    icon.pixels.assign(canvas.Pixels(), canvas.Pixels() + size_t(size) * size);
    return icon;
}

// The wide frame with rain over a fake desktop, as the dock would sit on it.
void PaintWideFrame(Canvas& screen, const FrameGeometry& g) {
    RECT bars[4];
    FrameBarRects(g, bars);
    RainGlyphs glyphs;
    glyphs.Build(int(lroundf(13.0f * g.scale)), int(lroundf(16.0f * g.scale)));
    const ZoneLayout& layout = ZoneLayouts()[4];  // Grid
    const TopBarContent content = {L"VenomMind", L"Work", &layout};
    PaintFakeDesktop(screen, ComputeZones(layout, g.inner, g.scale), 0);
    for (int i = 0; i < 4; ++i) {
        Canvas bar;
        bar.Resize(bars[i].right - bars[i].left, bars[i].bottom - bars[i].top);
        PaintFrameBar(bar, g, bars[i], i == 0, content, FrameButton::None);
        RECT area;
        RainFlow flow;
        RainField rain;
        if (FrameRainArea(g, bars[i], i, area, flow)) {
            rain.Reset(area, flow, glyphs, 77u + unsigned(i));
            for (int step = 0; step < 40; ++step) rain.Step(0.05f);
            rain.Draw(bar, glyphs, RainColor::Venom);
        }
        screen.Draw(bar, bars[i].left, bars[i].top);
    }
}

}  // namespace

int RunDockTest(const std::wstring& outDir) {
    CreateDirectoryW(outDir.c_str(), nullptr);
    LogOpen((outDir + L"\\dock.txt").c_str());
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);  // shell items want an apartment
    bool pass = true;
    const auto check = [&](bool ok, const char* what) {
        Log("%s  %s", ok ? "ok  " : "FAIL", what);
        pass = pass && ok;
    };

    // ---- Layout and hit-testing ----
    const int clock = DockClockWidth(1.0f);
    const DockLayout layout = LayoutDock(7, 3, 1.0f, 2400, clock);
    Log("Dock at 100%%: %dx%d, pill %ldx%ld, icons %d px, clock %d px", layout.width, layout.height,
        layout.pill.right - layout.pill.left, layout.pill.bottom - layout.pill.top, layout.iconSize, clock);
    check(layout.height == DockBandHeight(1.0f), "the dock is exactly as tall as the band it needs");
    check(layout.pill.left > 0 && layout.pill.top > 0 && layout.pill.right < layout.width &&
              layout.pill.bottom < layout.height,
          "the pill leaves room for its glow inside the window");
    const auto centre = [](const RECT& r) { return POINT{(r.left + r.right) / 2, (r.top + r.bottom) / 2}; };
    bool appsHit = true;
    bool ordered = true;
    for (size_t i = 0; i < layout.apps.size(); ++i) {
        const DockHit hit = DockHitTest(layout, centre(layout.apps[i]));
        appsHit = appsHit && hit.part == DockPart::App && hit.index == int(i);
        if (i > 0) ordered = ordered && layout.apps[i - 1].right <= layout.apps[i].left;
    }
    check(appsHit && ordered, "every app cell is hit at its centre, in order, without overlap");
    bool desktopsHit = true;
    for (size_t i = 0; i < layout.desktops.size(); ++i) {
        const DockHit hit = DockHitTest(layout, centre(layout.desktops[i]));
        desktopsHit = desktopsHit && hit.part == DockPart::Desktop && hit.index == int(i);
    }
    check(desktopsHit && layout.desktops.size() == 3, "each desktop dot switches to its own desktop");
    check(DockHitTest(layout, centre(layout.start)).part == DockPart::Start &&
              DockHitTest(layout, centre(layout.search)).part == DockPart::Search &&
              DockHitTest(layout, centre(layout.volume)).part == DockPart::Volume &&
              DockHitTest(layout, centre(layout.clock)).part == DockPart::Clock,
          "Start, search, volume and the clock are where they're drawn");
    check(DockHitTest(layout, {layout.apps[2].left + 3, layout.pill.top + 1}).part == DockPart::App &&
              DockHitTest(layout, {layout.apps[2].left + 3, layout.pill.top - 2}).part == DockPart::None,
          "the whole pill height counts, the glow around it doesn't");
    check(layout.start.right <= layout.search.left && layout.separators.size() == 3 &&
              layout.search.right <= layout.separators[0] && layout.separators[0] <= layout.apps.front().left &&
              layout.apps.back().right <= layout.separators[1] && layout.separators[1] <= layout.desktops.front().left &&
              layout.desktops.back().right <= layout.separators[2] && layout.separators[2] <= layout.volume.left &&
              layout.volume.right <= layout.clock.left,
          "groups run Start, search | apps | desktops | volume, clock");
    const DockLayout empty = LayoutDock(0, 1, 1.0f, 2400, clock);
    check(empty.apps.empty() && empty.separators.size() == 2 && empty.width < layout.width,
          "with no apps the dock shrinks and drops a separator");
    const DockLayout crowded = LayoutDock(30, 4, 1.0f, 1400, clock);
    check(crowded.width <= 1400 && crowded.iconSize >= 16, "a crowded dock squeezes its apps to fit the screen");
    Log("Crowded: 30 apps in %d px with %d px icons", crowded.width, crowded.iconSize);

    // ---- The frame makes room ----
    const RECT monitor = {0, 0, 2560, 1440};
    FrameStyle slimStyle;
    slimStyle.dockBand = DockBandHeight(1.0f);
    FrameGeometry slim = FrameSizes(monitor, 1.0f, slimStyle);
    slim.bottom = std::max(slim.bottom, int(lroundf(slimStyle.dockBand * slim.scale)));  // as ShellFrame does
    check(slim.bottom == DockBandHeight(1.0f), "the slim frame's bottom band grows to hold the dock");
    FrameStyle wideStyle;
    wideStyle.wide = true;
    wideStyle.margin = 1;
    wideStyle.dockBand = DockBandHeight(1.0f);
    FrameGeometry wide = FrameSizes(monitor, 1.0f, wideStyle);
    wide.bottom = std::max(wide.bottom, int(lroundf(wideStyle.dockBand * wide.scale)));
    check(wide.bottom == 96, "the wide medium margin already has room, so it stays as it was");
    wide.inner = {monitor.left + wide.side, monitor.top + wide.top, monitor.right - wide.side, monitor.bottom - wide.bottom};

    // ---- A render over the wide frame with the rain ----
    Canvas screen;
    screen.Resize(monitor.right, monitor.bottom);
    PaintWideFrame(screen, wide);
    const RECT slot = {monitor.left, wide.inner.bottom, monitor.right, monitor.bottom};
    const int iconSize = layout.iconSize;
    const Rgba tints[] = {{0.95f, 0.75f, 0.20f, 1.0f}, {0.10f, 0.55f, 0.95f, 1.0f}, {0.85f, 0.45f, 0.30f, 1.0f},
                          {0.20f, 0.20f, 0.25f, 1.0f}, {0.20f, 0.65f, 0.95f, 1.0f}, {0.55f, 0.30f, 0.90f, 1.0f},
                          {0.25f, 0.80f, 0.45f, 1.0f}};
    std::vector<IconImage> fakeIcons;
    for (const Rgba& tint : tints) fakeIcons.push_back(FakeIcon(iconSize, tint));
    std::vector<DockApp> apps(7);
    const wchar_t* names[] = {L"Files", L"Browser", L"Claude", L"Terminal", L"Code", L"Music", L"Chat"};
    for (size_t i = 0; i < apps.size(); ++i) {
        apps[i].name = names[i];
        apps[i].pinned = i < 4;
    }
    apps[0].windows = {{nullptr, L"", 0}};
    apps[1].windows = {{nullptr, L"", 0}, {nullptr, L"", 1}};
    apps[1].active = true;
    apps[3].windows = {{nullptr, L"", 0}};
    apps[3].attention = true;
    apps[4].windows = {{nullptr, L"", 0}};
    apps[6].windows = {{nullptr, L"", 0}};
    std::vector<const IconImage*> icons;
    for (size_t i = 0; i < apps.size(); ++i) icons.push_back(i == 5 ? nullptr : &fakeIcons[i]);  // Music: letter tile
    DockState state;
    state.currentDesktop = 1;
    state.volume = 0.45f;
    state.time = L"22:58";
    state.date = L"28/12/2026";
    state.hover = {DockPart::App, 4};
    state.pulse = 1.0f;

    Canvas dock;
    dock.Resize(layout.width, layout.height);
    PaintDock(dock, layout, apps, icons, state);
    const POINT at = {slot.left + (slot.right - slot.left - layout.width) / 2,
                      slot.top + (slot.bottom - slot.top - layout.height) / 2};
    check(at.y >= slot.top && at.y + layout.height <= slot.bottom, "the dock fits inside the bottom margin");
    const auto alpha = [&](int x, int y) { return dock.Pixels()[size_t(y) * dock.Width() + x] >> 24; };
    check(alpha(layout.width / 2, layout.height / 2) > 230 && alpha(0, 0) == 0,
          "the pill is solid glass and its corners are see-through (clicks pass)");
    screen.Draw(dock, at.x, at.y);

    // The preview of an app with three windows, just above it, with fake window pictures where DWM would draw.
    DockApp browser = apps[1];
    browser.windows = {{nullptr, L"VenomMind \u2014 docs", 0}, {nullptr, L"Venom Board \u2014 sprint plan", 1},
                       {nullptr, L"A very long page title that has to be cut to fit the preview", 2}};
    const PreviewLayout preview = LayoutPreview(browser, 1.0f);
    check(preview.cells.size() == 3 && preview.thumbs.size() == 3 && preview.width > 3 * 200,
          "the preview has a thumbnail per window");
    bool onClose = false;
    check(PreviewCellAt(preview, centre(preview.thumbs[1]), &onClose) == 1 && !onClose,
          "clicking a thumbnail picks that window");
    check(PreviewCellAt(preview, centre(preview.closes[2]), &onClose) == 2 && onClose, "each thumbnail has its own ×");
    Canvas previewCanvas;
    previewCanvas.Resize(preview.width, preview.height);
    PaintPreview(previewCanvas, preview, browser, &fakeIcons[1], {true, true, false}, 1, false, 1.0f);
    for (size_t i = 0; i < 2; ++i) {
        // Stand-ins for the live thumbnails DWM draws.
        const RECT& t = preview.thumbs[i];
        const LONG h = (t.right - t.left) * 9 / 16;
        const LONG top = t.top + (t.bottom - t.top - h) / 2;
        previewCanvas.FillRoundRect(float(t.left), float(top), float(t.right), float(top + h), 0.0f, {0.93f, 0.94f, 0.96f, 1.0f});
        previewCanvas.FillRoundRect(float(t.left), float(top), float(t.right), float(top + 12), 0.0f, {0.80f, 0.82f, 0.86f, 1.0f});
        for (LONG y = top + 22; y + 4 < top + h - 6; y += 12)
            previewCanvas.FillRoundRect(float(t.left + 10), float(y), float(t.left + 10 + (t.right - t.left - 20) * (60 + (y * 7) % 35) / 100),
                                        float(y + 4), 2.0f, {0.55f, 0.60f, 0.70f, 1.0f});
    }
    const RECT cell = layout.apps[1];
    const int previewX = std::clamp(int(at.x + (cell.left + cell.right) / 2 - preview.width / 2), 8,
                                    int(monitor.right) - preview.width - 8);
    const int previewY = at.y + layout.pill.top - preview.height - 6;
    screen.Draw(previewCanvas, previewX, previewY);
    // A crimson hairline stands in for the border DWM draws round the real preview window.
    screen.StrokeRoundRect(float(previewX), float(previewY), float(previewX + preview.width),
                           float(previewY + preview.height), 8.0f, 1.0f, venom::kFang);

    const int cropWidth = std::min(int(monitor.right), layout.width + 900);
    const int cropX = (int(monitor.right) - cropWidth) / 2;
    const int cropY = previewY - 20;
    SaveCrop(screen, cropX, cropY, cropWidth, int(monitor.bottom) - cropY, outDir + L"\\dock_preview.png");
    SaveCrop(screen, 0, int(monitor.bottom) - 300, int(monitor.right), 300, outDir + L"\\dock_band.png");
    SavePng(outDir + L"\\dock_screen.png", screen.Bytes(), UINT(screen.Width()), UINT(screen.Height()));

    // Hovering the volume shows its label instead.
    DockApp label;
    label.name = L"Volume 45%  \u00b7  scroll to change";
    const PreviewLayout labelLayout = LayoutPreview(label, 1.0f);
    check(labelLayout.cells.empty() && labelLayout.width > 150 && labelLayout.height < 40,
          "buttons get a one-line label, not a preview");

    // At 150%, crisp: everything scales together.
    const DockLayout big = LayoutDock(7, 3, 1.5f, 3600, DockClockWidth(1.5f));
    check(big.height == DockBandHeight(1.5f) && big.iconSize == int(lroundf(30.0f * 1.5f)), "the dock scales with the display");
    std::vector<IconImage> bigIcons;
    for (const Rgba& tint : tints) bigIcons.push_back(FakeIcon(big.iconSize, tint));
    std::vector<const IconImage*> bigIconPointers;
    for (size_t i = 0; i < apps.size(); ++i) bigIconPointers.push_back(i == 5 ? nullptr : &bigIcons[i]);
    Canvas bigDock;
    bigDock.Resize(big.width + 40, big.height + 40);
    bigDock.FillRoundRect(0, 0, float(bigDock.Width()), float(bigDock.Height()), 0.0f, venom::kVoid);
    Canvas bigPill;
    bigPill.Resize(big.width, big.height);
    state.hover = {DockPart::Volume, -1};
    state.muted = false;
    PaintDock(bigPill, big, apps, bigIconPointers, state);
    bigDock.Draw(bigPill, 20, 20);
    SavePng(outDir + L"\\dock_150.png", bigDock.Bytes(), UINT(bigDock.Width()), UINT(bigDock.Height()));

    // ---- This PC, read-only: counts only, no names or titles ----
    const std::vector<std::wstring> pins = TaskbarPinnedShortcuts();
    int readable = 0;
    int withId = 0;
    for (const std::wstring& pin : pins) {
        ShortcutInfo info;
        if (ReadShortcut(pin, info)) {
            ++readable;
            withId += info.appId.empty() ? 0 : 1;
        }
    }
    Log("Windows taskbar: %zu pinned shortcuts, %d readable, %d with an app ID; auto-hide is %s", pins.size(), readable,
        withId, TaskbarAutoHide() ? "on" : "off");
    check(readable == int(pins.size()), "every taskbar pin can be read to seed the dock");

    DockModel model;
    model.SetPins(pins);
    std::set<HWND> flashing;
    model.Refresh([](HWND window) {
        DWORD cloaked = 0;
        DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
        return cloaked == 0;
    }, GetForegroundWindow(), flashing);
    size_t running = 0, windows = 0, pinnedRunning = 0, withIcon = 0, active = 0;
    std::vector<IconImage> realIcons(model.Apps().size());
    for (size_t i = 0; i < model.Apps().size(); ++i) {
        const DockApp& app = model.Apps()[i];
        if (!app.windows.empty()) {
            ++running;
            windows += app.windows.size();
            pinnedRunning += app.pinned ? 1 : 0;
        }
        active += app.active ? 1 : 0;
        bool ok = ShellIcon(app.iconSource, iconSize, realIcons[i]);
        if (!ok && !app.exe.empty()) ok = ShellIcon(app.exe, iconSize, realIcons[i]);
        if (!ok && app.iconWindow) ok = WindowIcon(app.iconWindow, iconSize, realIcons[i]);
        withIcon += ok ? 1 : 0;
    }
    Log("Dock on this desktop: %zu apps (%zu pinned), %zu running with %zu windows, %zu running apps grouped with "
        "their pin, %zu with the focus; icons for %zu",
        model.Apps().size(), pins.size(), running, windows, pinnedRunning, active, withIcon);
    check(model.Apps().size() >= pins.size(), "every pin is on the dock");
    check(withIcon == model.Apps().size(), "every app on the dock has a real icon");
    size_t opaque = 0, partial = 0;
    for (const IconImage& icon : realIcons) {
        for (const uint32_t p : icon.pixels) {
            const uint32_t a = p >> 24;
            opaque += a == 255;
            partial += a > 0 && a < 255;
        }
    }
    check(partial > 0 && opaque > 0, "real icons keep their soft edges (alpha survived)");

    // The real dock, with this PC's icons (no titles are drawn).
    const DockLayout real = LayoutDock(int(model.Apps().size()), 2, 1.0f, 2400, clock);
    std::vector<const IconImage*> realPointers;
    for (const IconImage& icon : realIcons) realPointers.push_back(icon.Empty() ? nullptr : &icon);
    DockState realState;
    {
        wchar_t time[64] = {}, date[64] = {};
        GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, nullptr, nullptr, time, ARRAYSIZE(time));
        GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, nullptr, nullptr, date, ARRAYSIZE(date), nullptr);
        realState.time = time;
        realState.date = date;
        check(Canvas::MeasureString(time, 13, FW_SEMIBOLD, venom::UiFont()) + 8.0f <= float(clock) &&
                  Canvas::MeasureString(date, 11, FW_NORMAL, venom::UiFont()) + 8.0f <= float(clock),
              "today's time and date fit the clock");
    }
    MasterVolume volume;
    float level = 0.0f;
    bool muted = false;
    realState.hasAudio = volume.Read(level, muted);
    realState.volume = level;
    realState.muted = muted;
    Log("Speakers: %s, volume %d%%%s", realState.hasAudio ? "found" : "none", int(lroundf(level * 100.0f)),
        muted ? ", muted" : "");
    Canvas realDock;
    realDock.Resize(real.width + 40, real.height + 40);
    realDock.FillRoundRect(0, 0, float(realDock.Width()), float(realDock.Height()), 0.0f, {0.10f, 0.12f, 0.20f, 1.0f});
    Canvas realPill;
    realPill.Resize(real.width, real.height);
    PaintDock(realPill, real, model.Apps(), realPointers, realState);
    realDock.Draw(realPill, 20, 20);
    SavePng(outDir + L"\\dock_real.png", realDock.Bytes(), UINT(realDock.Width()), UINT(realDock.Height()));
    Log("Fonts: icons %ls, interface %ls", venom::IconFont(), venom::UiFont());
    check(wcscmp(venom::IconFont(), L"Segoe UI") != 0, "Windows' symbol font is installed for the dock's glyphs");

    Log(pass ? "RESULT: PASS" : "RESULT: FAIL");
    volume = MasterVolume();
    if (SUCCEEDED(hr)) CoUninitialize();
    return pass ? 0 : 7;
}

int RunSysInfoTest(const std::wstring& outDir) {
    CreateDirectoryW(outDir.c_str(), nullptr);
    LogOpen((outDir + L"\\sysinfo.txt").c_str());
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool pass = true;
    const auto check = [&](bool ok, const char* what) {
        Log("%s  %s", ok ? "ok  " : "FAIL", what);
        pass = pass && ok;
    };

    // ---- Where the orbs go, for every frame size ----
    const RECT monitor = {0, 0, 2560, 1440};
    const auto geometry = [&](bool wide, int margin, float scale) {
        FrameStyle style;
        style.wide = wide;
        style.margin = margin;
        const RECT scaled = {0, 0, LONG(monitor.right * scale / 1.0f), LONG(monitor.bottom * scale)};
        FrameGeometry g = FrameSizes(scaled, scale, style);
        g.inner = {scaled.left + g.side, scaled.top + g.top, scaled.right - g.side, scaled.bottom - g.bottom};
        return g;
    };
    const auto noOverlap = [](const OrbsLayout& layout) {
        for (size_t i = 1; i < layout.items.size(); ++i)
            if (layout.items[i - 1].right > layout.items[i].left) return false;
        return true;
    };
    // The orbs must stay off the top bar's own buttons and text.
    const auto clearOfButtons = [](const FrameGeometry& g, const OrbsLayout& layout, int barWidth) {
        for (LONG y = layout.box.top; y < layout.box.bottom; y += 2)
            for (LONG x = layout.box.left; x < layout.box.right; x += 2)
                if (FrameButtonAt(g, barWidth, {x, y}) != FrameButton::None) return false;
        return true;
    };

    const FrameGeometry medium = geometry(true, 1, 1.0f);
    const int barWidth = monitor.right;
    const OrbsLayout orbs = LayoutOrbs(medium, barWidth);
    Log("Medium margin: orbs %.0f px across in a %ldx%ld box", orbs.radius * 2.0f, orbs.box.right - orbs.box.left,
        orbs.box.bottom - orbs.box.top);
    check(!orbs.compact && orbs.items.size() == kOrbCount && noOverlap(orbs), "the wide margin gets four orbs side by side");
    check(orbs.box.top >= 0 && orbs.box.bottom <= medium.top - medium.strip,
          "the orbs sit in the margin, above the strip");
    check(std::abs((orbs.box.left + orbs.box.right) / 2 - barWidth / 2) <= 1, "the orbs are centred");
    check(clearOfButtons(medium, orbs, barWidth), "the orbs don't cover the top bar's buttons");

    const FrameGeometry smallMargin = geometry(true, 0, 1.0f);
    const OrbsLayout smallMarginOrbs = LayoutOrbs(smallMargin, barWidth);
    check(smallMarginOrbs.compact && smallMarginOrbs.items.size() == kOrbCount && noOverlap(smallMarginOrbs) &&
              smallMarginOrbs.box.bottom <= smallMargin.top - smallMargin.strip && clearOfButtons(smallMargin, smallMarginOrbs, barWidth),
          "the small margin gets chips instead, still in the margin");
    const FrameGeometry slim = geometry(false, 1, 1.0f);
    const OrbsLayout slimOrbs = LayoutOrbs(slim, barWidth);
    check(slimOrbs.compact && slimOrbs.items.size() == kOrbCount && slimOrbs.box.top >= 0 &&
              slimOrbs.box.bottom <= slim.top && clearOfButtons(slim, slimOrbs, barWidth),
          "the slim frame gets chips in its strip, clear of the brand and buttons");
    const FrameGeometry large = geometry(true, 2, 1.0f);
    const OrbsLayout largeOrbs = LayoutOrbs(large, barWidth);
    check(!largeOrbs.compact && largeOrbs.radius * 2.0f <= 88.5f, "orbs stop growing in the large margin");
    const FrameGeometry hidpi = geometry(true, 1, 1.5f);
    const OrbsLayout hidpiOrbs = LayoutOrbs(hidpi, int(barWidth * 1.5f));
    check(!hidpiOrbs.compact && fabsf(hidpiOrbs.radius - orbs.radius * 1.5f) < 2.0f, "the orbs scale with the display");

    // ---- Renders over the wide frame with the rain ----
    SystemStats fake;
    fake.valid = true;
    fake.cpu = 37.0f;  // no CPU temperature, as on this desktop
    fake.gpu = 82.0f;
    fake.gpuTemp = 71.0f;
    fake.ram = 46.0f;
    fake.ramUsedGb = 14.7f;
    fake.ramTotalGb = 32.0f;
    fake.vram = 23.0f;
    fake.vramUsedGb = 1.8f;
    fake.vramTotalGb = 8.0f;
    const auto render = [&](const FrameGeometry& g, const SystemStats& stats, bool hover, const std::wstring& name) {
        Canvas screen;
        screen.Resize(g.monitor.right, g.monitor.bottom);
        PaintFakeDesktop(screen, ComputeZones(ZoneLayouts()[0], g.inner, g.scale), 0);
        RECT bars[4];
        FrameBarRects(g, bars);
        Canvas bar;
        bar.Resize(bars[0].right - bars[0].left, bars[0].bottom - bars[0].top);
        const TopBarContent content = {L"VenomMind", L"Work", &ZoneLayouts()[0]};
        PaintFrameBar(bar, g, bars[0], true, content, FrameButton::None);
        RECT area;
        RainFlow flow;
        if (FrameRainArea(g, bars[0], 0, area, flow)) {
            RainGlyphs glyphs;
            glyphs.Build(int(lroundf(13.0f * g.scale)), int(lroundf(16.0f * g.scale)));
            RainField rain;
            rain.Reset(area, flow, glyphs, 99u);
            for (int step = 0; step < 40; ++step) rain.Step(0.05f);
            rain.Draw(bar, glyphs, RainColor::Venom);
        }
        const OrbsLayout layout = LayoutOrbs(g, bar.Width());
        Canvas overlay;
        overlay.Resize(layout.box.right - layout.box.left, layout.box.bottom - layout.box.top);
        PaintOrbs(overlay, layout, stats, g.scale, hover);
        bar.Draw(overlay, layout.box.left, layout.box.top);
        screen.Draw(bar, bars[0].left, bars[0].top);
        const int width = std::min(screen.Width(), int(layout.box.right - layout.box.left) + int(700 * g.scale));
        SaveCrop(screen, (screen.Width() - width) / 2, 0, width, std::min(screen.Height(), g.top + int(60 * g.scale)),
                 outDir + L"\\" + name);
        return layout;
    };
    render(medium, fake, false, L"orbs_medium.png");
    render(medium, fake, true, L"orbs_medium_hover.png");
    render(smallMargin, fake, false, L"orbs_small_margin.png");
    render(slim, fake, false, L"orbs_slim.png");
    render(hidpi, fake, false, L"orbs_150.png");
    SystemStats empty;
    render(medium, empty, false, L"orbs_no_data.png");

    // ---- This PC, read-only ----
    SystemMonitor reader;
    check(reader.Open(), "Windows reports CPU times");
    Sleep(1000);
    LARGE_INTEGER frequency, start, end;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&start);
    const SystemStats real = reader.Sample();
    QueryPerformanceCounter(&end);
    const double sampleMs = double(end.QuadPart - start.QuadPart) * 1000.0 / double(frequency.QuadPart);
    Log("Sources: %ls", reader.SourceSummary().c_str());
    Log("Reading: CPU %.0f%% at %.0f C, GPU %.0f%% at %.1f C, RAM %.0f%% (%.1f of %.1f GB), VRAM %.0f%% (%.1f of %.1f GB)",
        real.cpu, real.cpuTemp, real.gpu, real.gpuTemp, real.ram, real.ramUsedGb, real.ramTotalGb, real.vram,
        real.vramUsedGb, real.vramTotalGb);
    Log("One reading took %.1f ms (on the monitor's own thread, once a second)", sampleMs);
    check(real.valid && real.cpu >= 0.0f && real.cpu <= 100.0f, "CPU use is read");
    check(real.ram > 0.0f && real.ram <= 100.0f && real.ramTotalGb > 1.0f, "memory use is read");
    check(real.gpu >= 0.0f && real.gpu <= 100.0f, "GPU use is read from Windows' GPU counters");
    check(real.vram >= 0.0f && real.vramTotalGb > 0.5f, "video memory use is read");
    check(real.gpuTemp > 10.0f && real.gpuTemp < 110.0f, "GPU temperature is read from the display driver");
    Log("%s  CPU temperature %s", real.cpuTemp > 0.0f ? "ok  " : "note",
        real.cpuTemp > 0.0f ? "is read" : "isn't available on this PC without a hardware monitor app (shown as a dash)");
    check(sampleMs < 500.0, "a reading is quick enough for once a second");
    reader.Close();
    render(medium, real, false, L"orbs_real.png");

    // The background reader delivers readings, pauses and stops cleanly.
    SystemMonitor background;
    background.SetActive(true, nullptr, 0);
    Sleep(1800);
    const SystemStats live = background.Latest();
    background.SetActive(false, nullptr, 0);
    Sleep(100);
    QueryPerformanceCounter(&start);
    background.Stop();
    QueryPerformanceCounter(&end);
    const double stopMs = double(end.QuadPart - start.QuadPart) * 1000.0 / double(frequency.QuadPart);
    check(live.valid && live.ram > 0.0f, "the background reader delivers readings");
    check(stopMs < 200.0, "pausing and stopping the reader doesn't keep anyone waiting");

    Log(pass ? "RESULT: PASS" : "RESULT: FAIL");
    if (SUCCEEDED(hr)) CoUninitialize();
    return pass ? 0 : 8;
}

int RunBubblesTest(const std::wstring& outDir) {
    CreateDirectoryW(outDir.c_str(), nullptr);
    LogOpen((outDir + L"\\bubbles.txt").c_str());
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool pass = true;
    const auto check = [&](bool ok, const char* what) {
        Log("%s  %s", ok ? "ok  " : "FAIL", what);
        pass = pass && ok;
    };

    // ---- Sizes for each frame ----
    const RECT monitor = {0, 0, 2560, 1440};
    const auto geometry = [&](bool wide, int margin) {
        FrameStyle style;
        style.wide = wide;
        style.margin = margin;
        FrameGeometry g = FrameSizes(monitor, 1.0f, style);
        g.inner = {monitor.left + g.side, monitor.top + g.top, monitor.right - g.side, monitor.bottom - g.bottom};
        return g;
    };
    const FrameGeometry g = geometry(true, 1);
    const BubbleMetrics m = MeasureBubbles(g);
    Log("Medium margin: bubbles %d px, cells %dx%d, names %s", m.diameter, m.cellWidth, m.cellHeight,
        m.labels ? "under the bubbles" : "on hover");
    check(m.usable && m.labels && m.cellWidth <= g.side - 12 && m.cellHeight <= g.top - g.strip - 12,
          "the medium margin fits bubbles with their names under them");
    const BubbleMetrics smallMetrics = MeasureBubbles(geometry(true, 0));
    check(smallMetrics.usable && !smallMetrics.labels && smallMetrics.diameter <= 56 - 12,
          "the small margin fits smaller bubbles, names on hover");
    check(!MeasureBubbles(geometry(false, 1)).usable, "the slim frame has no room for bubbles");

    // ---- The + button ----
    RECT bars[4];
    FrameBarRects(g, bars);
    const int topWidth = bars[0].right - bars[0].left;
    const int stripY = g.top - g.strip / 2;
    int addAt = -1, settingsAt = -1;
    for (int x = topWidth - 1; x > topWidth / 2; --x) {
        const FrameButton button = FrameButtonAt(g, topWidth, {x, stripY});
        if (button == FrameButton::Add && addAt < 0) addAt = x;
        if (button == FrameButton::Settings && settingsAt < 0) settingsAt = x;
    }
    check(addAt > 0 && addAt < settingsAt, "the + sits just left of the settings gear");
    bool slimAdd = false;
    const FrameGeometry slim = geometry(false, 1);
    for (int x = 0; x < topWidth; x += 2) slimAdd = slimAdd || FrameButtonAt(slim, topWidth, {x, slim.top / 2}) == FrameButton::Add;
    check(!slimAdd, "the slim frame has no + (bubbles need the wide margin)");

    // ---- Where bubbles may go ----
    RECT areas[4];
    for (int band = 0; band < 4; ++band) areas[band] = BubbleArea(g, bars[band], band);
    check(areas[0].bottom <= g.top - g.strip && areas[0].top >= 0, "top bubbles stay above the strip");
    check(areas[3].top >= g.radius, "bottom bubbles stay clear of the opening's rounded corners");
    check(areas[1].right <= bars[1].right - bars[1].left && areas[2].right <= bars[2].right - bars[2].left,
          "side bubbles stay inside their bands");
    check(BandAllows(ShortcutKind::App, 1) && BandAllows(ShortcutKind::App, 2) && !BandAllows(ShortcutKind::App, 0) &&
              BandAllows(ShortcutKind::Folder, 0) && BandAllows(ShortcutKind::Folder, 3) &&
              !BandAllows(ShortcutKind::Folder, 1),
          "apps live in the side bands, folders in the top and bottom bands");

    // ---- Sockets ----
    const OrbsLayout orbs = LayoutOrbs(g, topWidth);
    const DockLayout dockLayout = LayoutDock(4, 2, 1.0f, 2400, DockClockWidth(1.0f));
    const RECT dockArea = {(monitor.right - dockLayout.width) / 2, g.inner.bottom + (g.bottom - dockLayout.height) / 2,
                           (monitor.right + dockLayout.width) / 2,
                           g.inner.bottom + (g.bottom - dockLayout.height) / 2 + dockLayout.height};
    RECT dockLocal = dockArea;
    OffsetRect(&dockLocal, -bars[3].left, -bars[3].top);
    std::vector<RECT> sockets[4];
    sockets[0] = BandSockets(areas[0], m, {orbs.box});
    sockets[1] = BandSockets(areas[1], m, {});
    sockets[2] = BandSockets(areas[2], m, {});
    sockets[3] = BandSockets(areas[3], m, {dockLocal});
    Log("Sockets: %zu along the top, %zu down each side, %zu along the bottom", sockets[0].size(), sockets[1].size(),
        sockets[3].size());
    // Evenly spaced: every gap between neighbours the same (to a pixel), and the band's two ends alike.
    const auto even = [](const std::vector<RECT>& list, const RECT& area, bool across) {
        if (list.size() < 2) return false;
        const auto lead = [&](const RECT& r) { return across ? r.left : r.top; };
        const auto tail = [&](const RECT& r) { return across ? r.right : r.bottom; };
        const LONG gap = lead(list[1]) - tail(list[0]);
        for (size_t i = 1; i < list.size(); ++i)
            if (abs(lead(list[i]) - tail(list[i - 1]) - gap) > 1) return false;
        const LONG before = lead(list.front()) - (across ? area.left : area.top);
        const LONG after = (across ? area.right : area.bottom) - tail(list.back());
        return gap > 0 && abs(before - after) <= 1;
    };
    check(sockets[1].size() >= 8 && sockets[1].size() == sockets[2].size() && even(sockets[1], areas[1], false),
          "the side bands have evenly spaced sockets, the same on both sides");
    RECT overlap;
    bool clearOfOrbs = true;
    for (const RECT& socket : sockets[0]) clearOfOrbs = clearOfOrbs && !IntersectRect(&overlap, &socket, &orbs.box);
    bool clearOfDock = true;
    for (const RECT& socket : sockets[3]) clearOfDock = clearOfDock && !IntersectRect(&overlap, &socket, &dockLocal);
    check(sockets[0].size() > 15 && clearOfOrbs, "the top band's sockets leave the system orbs clear");
    check(sockets[3].size() > 10 && clearOfDock, "the bottom band's sockets leave the dock clear");
    const std::vector<RECT> open = BandSockets(areas[0], m, {});
    check(even(open, areas[0], true), "with nothing in the way, the top band's sockets are evenly spaced too");
    // Each bubble takes the free socket nearest where it was put.
    std::vector<bool> taken;
    const std::vector<int> assigned = AssignSockets(
        sockets[1], {CellAt(areas[1], m, 0.0f, 0.0f), CellAt(areas[1], m, 0.0f, 0.0f), CellAt(areas[1], m, 0.5f, 1.0f)}, taken);
    check(assigned[0] == 0 && assigned[1] == 1 && assigned[2] == int(sockets[1].size()) - 1,
          "a bubble takes the socket nearest where it was put; a second one there takes the next one along");
    float fx = 0.0f, fy = 0.0f;
    const RECT& third = sockets[1][3];
    FractionsOf(areas[1], m, {(third.left + third.right) / 2, (third.top + third.bottom) / 2}, fx, fy);
    std::vector<bool> fresh;
    check(AssignSockets(sockets[1], {CellAt(areas[1], m, fx, fy)}, fresh)[0] == 3, "a bubble's saved place brings it back to its socket");
    std::vector<bool> full(sockets[1].size(), true);
    check(AssignSockets(sockets[1], {CellAt(areas[1], m, 0.5f, 0.5f)}, full)[0] == -1,
          "a full band has no socket left (the bubble moves to the other band of its pair)");
    check(!BandSockets(BubbleArea(geometry(true, 0), bars[1], 1), MeasureBubbles(geometry(true, 0)), {}).empty(),
          "the small margin has sockets too");

    // ---- Previews beside a bubble ----
    const RECT leftBubble = {18, 600, 78, 660};
    const RECT toRight = PlacePreview({600, 200}, leftBubble, PreviewSide::Right, monitor, 6);
    check(toRight.left == leftBubble.right + 6 && abs((toRight.top + toRight.bottom) / 2 - 630) <= 1,
          "a left-band bubble's preview opens to its right, level with it");
    const RECT rightBubble = {2482, 600, 2542, 660};
    const RECT toLeft = PlacePreview({600, 200}, rightBubble, PreviewSide::Left, monitor, 6);
    check(toLeft.right == rightBubble.left - 6, "a right-band bubble's preview opens to its left");
    const RECT low = PlacePreview({600, 200}, {18, 1400, 78, 1440}, PreviewSide::Right, monitor, 6);
    check(low.bottom <= monitor.bottom - 6, "a preview never runs off the screen");
    const RECT dockAbove = PlacePreview({300, 160}, {1200, 1380, 1260, 1430}, PreviewSide::Above, monitor, 6);
    check(dockAbove.bottom == 1374 && abs((dockAbove.left + dockAbove.right) / 2 - 1230) <= 1,
          "the dock's previews still open above it, centred");

    // ---- Finding an app bubble's windows ----
    wchar_t system[MAX_PATH] = {};
    GetSystemDirectoryW(system, MAX_PATH);
    const std::wstring notepad = std::wstring(system) + L"\\notepad.exe";
    check(_wcsicmp(ExeFromAppsFolderId(L"{1AC14E77-02E7-4E5D-B744-2EB1AE5198B7}\\notepad.exe").c_str(), notepad.c_str()) == 0 &&
              ExeFromAppsFolderId(L"Microsoft.WindowsCalculator_8wekyb3d8bbwe!App").empty(),
          "Start's exe-path app IDs resolve to the real exe; packaged ones stay app IDs");

    // ---- A render: the wide frame with rain, orbs, a dock, sockets and bubbles ----
    SystemStats stats;
    stats.cpu = 23.0f;
    stats.gpu = 41.0f;
    stats.gpuTemp = 58.0f;
    stats.ram = 52.0f;
    stats.ramUsedGb = 16.6f;
    stats.ramTotalGb = 32.0f;
    stats.vram = 30.0f;
    stats.vramUsedGb = 2.4f;
    stats.vramTotalGb = 8.0f;
    wchar_t windows[MAX_PATH] = {}, profile[MAX_PATH] = {};
    GetWindowsDirectoryW(windows, MAX_PATH);
    GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH);
    const std::wstring win = windows, home = profile;
    struct Item {
        ShortcutKind kind;
        const wchar_t* name;
        std::wstring target;
        int band;
        int socket;  // from the start of the band; negative counts from its end
        BubbleState state;
    };
    const Item items[] = {
        {ShortcutKind::App, L"Notepad", win + L"\\System32\\notepad.exe", 1, 0, BubbleState::Active},
        {ShortcutKind::App, L"Terminal", win + L"\\System32\\cmd.exe", 1, 1, BubbleState::Running},
        {ShortcutKind::App, L"Calculator", L"shell:AppsFolder\\Microsoft.WindowsCalculator_8wekyb3d8bbwe!App", 1, 3,
         BubbleState::Closed},
        {ShortcutKind::App, L"File Explorer", win + L"\\explorer.exe", 2, 0, BubbleState::Running},
        {ShortcutKind::Folder, L"Downloads", home + L"\\Downloads", 0, 0, BubbleState::Closed},
        {ShortcutKind::Folder, L"Documents", home + L"\\Documents", 0, 1, BubbleState::Closed},
        {ShortcutKind::Folder, L"Pictures", home + L"\\Pictures", 0, 2, BubbleState::Closed},
        {ShortcutKind::Folder, L"Windows", win, 3, -2, BubbleState::Closed},
        {ShortcutKind::Folder, L"Program Files", L"C:\\Program Files", 3, -1, BubbleState::Closed},
    };
    RainGlyphs glyphs;
    glyphs.Build(13, 16);
    const TopBarContent content = {L"VenomMind", L"Work", &ZoneLayouts()[0]};
    Canvas socketSprites[2][3];
    for (int kind = 0; kind < 2; ++kind)
        for (int look = 0; look < 3; ++look) {
            socketSprites[kind][look].Resize(m.cellWidth, m.cellHeight);
            PaintSocket(socketSprites[kind][look], m, kind == 0 ? ShortcutKind::App : ShortcutKind::Folder,
                        SocketLook(look), 1.0f);
        }
    int icons = 0;
    // `dragged`: the item being carried (its socket empties and the sockets it may go to open up), or -1.
    const auto render = [&](Canvas& screen, int dragged, int targetBar, int targetSocket, POINT ghostAt) {
        screen.Resize(monitor.right, monitor.bottom);
        PaintFakeDesktop(screen, ComputeZones(ZoneLayouts()[0], g.inner, 1.0f), 0);
        Canvas barCanvas[4];
        for (int i = 0; i < 4; ++i) {
            barCanvas[i].Resize(bars[i].right - bars[i].left, bars[i].bottom - bars[i].top);
            PaintFrameBar(barCanvas[i], g, bars[i], i == 0, content, i == 0 && dragged < 0 ? FrameButton::Add : FrameButton::None);
            RECT area;
            RainFlow flow;
            RainField rain;
            if (FrameRainArea(g, bars[i], i, area, flow)) {
                rain.Reset(area, flow, glyphs, 5u + unsigned(i));
                for (int step = 0; step < 40; ++step) rain.Step(0.05f);
                rain.Draw(barCanvas[i], glyphs, RainColor::Venom);
            }
        }
        Canvas orbCanvas;
        orbCanvas.Resize(orbs.box.right - orbs.box.left, orbs.box.bottom - orbs.box.top);
        PaintOrbs(orbCanvas, orbs, stats, 1.0f, false);
        barCanvas[0].Draw(orbCanvas, orbs.box.left, orbs.box.top);
        std::vector<bool> occupied[4];
        for (int band = 0; band < 4; ++band) occupied[band].assign(sockets[band].size(), false);
        const auto socketOf = [&](const Item& item) {
            return item.socket >= 0 ? item.socket : int(sockets[item.band].size()) + item.socket;
        };
        for (size_t i = 0; i < ARRAYSIZE(items); ++i)
            if (int(i) != dragged) occupied[items[i].band][socketOf(items[i])] = true;
        const ShortcutKind dragKind = dragged >= 0 ? items[dragged].kind : ShortcutKind::App;
        for (int band = 0; band < 4; ++band) {
            const int kind = band == 1 || band == 2 ? 0 : 1;
            for (size_t k = 0; k < sockets[band].size(); ++k) {
                if (occupied[band][k]) continue;
                SocketLook look = SocketLook::Idle;
                if (dragged >= 0 && BandAllows(dragKind, band))
                    look = band == targetBar && int(k) == targetSocket ? SocketLook::Target : SocketLook::Open;
                barCanvas[band].Draw(socketSprites[kind][int(look)], sockets[band][k].left, sockets[band][k].top);
            }
        }
        Canvas sprite;
        sprite.Resize(m.cellWidth, m.cellHeight);
        for (size_t i = 0; i < ARRAYSIZE(items); ++i) {
            const Item& item = items[i];
            IconImage icon;
            const bool haveIcon = ShellIcon(item.target, int(lroundf(m.diameter * 0.56f)), icon);
            if (dragged < 0) icons += haveIcon ? 1 : 0;
            PaintBubble(sprite, m, haveIcon ? &icon : nullptr, item.name, item.kind, (dragged < 0 && i == 5) || int(i) == dragged,
                        1.0f, item.state);
            if (int(i) == dragged) continue;
            const RECT& cell = sockets[item.band][socketOf(item)];
            barCanvas[item.band].Draw(sprite, cell.left, cell.top);
        }
        for (int i = 0; i < 4; ++i) screen.Draw(barCanvas[i], bars[i].left, bars[i].top);
        std::vector<DockApp> dockApps(4);
        for (size_t i = 0; i < dockApps.size(); ++i) dockApps[i].name = L"App";
        dockApps[0].windows = {{nullptr, L"", 0}};
        dockApps[0].active = true;
        DockState dockState;
        dockState.time = L"21:14";
        dockState.date = L"20/10/2026";
        Canvas dockCanvas;
        dockCanvas.Resize(dockLayout.width, dockLayout.height);
        PaintDock(dockCanvas, dockLayout, dockApps, {nullptr, nullptr, nullptr, nullptr}, dockState);
        screen.Draw(dockCanvas, dockArea.left, dockArea.top);
        if (dragged >= 0) {
            // The carried bubble, as the ghost window shows it.
            IconImage icon;
            const Item& item = items[dragged];
            const bool haveIcon = ShellIcon(item.target, int(lroundf(m.diameter * 0.56f)), icon);
            PaintBubble(sprite, m, haveIcon ? &icon : nullptr, item.name, item.kind, true, 1.0f, item.state);
            screen.Draw(sprite, ghostAt.x, ghostAt.y);
        }
    };
    {
        Canvas screen;
        render(screen, -1, -1, -1, {});
        SavePng(outDir + L"\\bubbles_screen.png", screen.Bytes(), UINT(screen.Width()), UINT(screen.Height()));
        SaveCrop(screen, 0, 0, 1100, 520, outDir + L"\\bubbles_top_left.png");
        SaveCrop(screen, screen.Width() - 1300, screen.Height() - 260, 1300, 260, outDir + L"\\bubbles_bottom_right.png");
    }
    check(icons == int(ARRAYSIZE(items)), "every bubble gets its real icon from Windows");
    {
        // Terminal carried across toward the right band's fourth socket.
        const RECT& target = sockets[2][3];
        const POINT ghost = {bars[2].left + target.left - 40, bars[2].top + target.top + 18};
        Canvas screen;
        render(screen, 1, 2, 3, ghost);
        SavePng(outDir + L"\\bubbles_drag.png", screen.Bytes(), UINT(screen.Width()), UINT(screen.Height()));
        SaveCrop(screen, 0, 0, 260, 900, outDir + L"\\bubbles_drag_left.png");
        SaveCrop(screen, screen.Width() - 360, 0, 360, 900, outDir + L"\\bubbles_drag_right.png");
    }

    // ---- Saved bubbles survive a save and load ----
    Settings original;
    Shortcut app;
    app.kind = ShortcutKind::App;
    app.target = L"shell:AppsFolder\\Microsoft.WindowsCalculator_8wekyb3d8bbwe!App";
    app.name = L"Calc | tools \u00e9";
    app.monitor = L"\\\\.\\DISPLAY2";
    app.band = 2;
    app.fx = 0.25f;
    app.fy = 0.75f;
    Shortcut folder;
    folder.kind = ShortcutKind::Folder;
    folder.target = L"C:\\Users\\Public";
    folder.name = L"Public";
    folder.band = 3;
    folder.fx = 0.5f;
    folder.fy = 0.0f;
    original.shortcuts = {app, folder};
    const std::wstring ini = outDir + L"\\bubbles.ini";
    DeleteFileW(ini.c_str());
    original.Save(ini);
    WritePrivateProfileStringW(L"Shortcuts", L"S2", L"folder|1|0.5|0.5||C:\\Temp|Wrong band", ini.c_str());
    Settings loaded;
    loaded.Load(ini);
    const bool same = loaded.shortcuts.size() == 3 && loaded.shortcuts[0].name == app.name &&
                      loaded.shortcuts[0].target == app.target && loaded.shortcuts[0].monitor == app.monitor &&
                      loaded.shortcuts[0].band == 2 && fabsf(loaded.shortcuts[0].fx - 0.25f) < 0.001f &&
                      fabsf(loaded.shortcuts[0].fy - 0.75f) < 0.001f && loaded.shortcuts[1].kind == ShortcutKind::Folder &&
                      loaded.shortcuts[1].target == folder.target;
    check(same, "bubbles (kind, target, name with a bar in it, monitor and place) survive a save and load");
    check(loaded.shortcuts.size() == 3 && loaded.shortcuts[2].band == 0, "a folder saved in an app band moves to the top band");

    // ---- The app picker ----
    std::vector<InstalledApp> fakeApps;
    for (const wchar_t* name : {L"Blender", L"Calculator", L"Claude", L"Code", L"Discord", L"Epic Games Launcher",
                                L"File Explorer", L"Firefox", L"Microsoft Edge", L"Notepad", L"OBS Studio", L"Paint",
                                L"Settings", L"Spotify", L"Steam", L"Terminal", L"Unreal Engine", L"Visual Studio Code"})
        fakeApps.push_back({name, std::wstring(L"shell:AppsFolder\\") + name});
    AppPickerView view;
    view.Build(1.0f);
    view.SetApps(&fakeApps);
    check(view.Matches() == int(fakeApps.size()) && view.Selected() == 0, "with nothing typed, every app is listed");
    view.SetFilter(L"co");
    const InstalledApp* top = view.Match(0);
    check(view.Matches() == 3 && top && top->name == L"Code", "typing narrows the list, names starting with it first");
    view.SetFilter(L"");
    view.MoveSelection(view.VisibleRows() + 2);
    check(view.Selected() == view.VisibleRows() + 2 && view.FirstRow() > 0, "moving the selection down scrolls to keep it in view");
    int match = -1;
    check(view.HitTest({view.Width() / 2, 114 + 40 * 2 + 20}, &match) == AppPickerView::Part::Row && match == view.FirstRow() + 2,
          "clicking a row picks that app");
    check(view.HitTest({40, view.Height() - 28}, &match) == AppPickerView::Part::Browse,
          "the footer offers Windows' own file picker for anything else");
    check(view.HitTest({view.Width() / 2, 20}, &match) == AppPickerView::Part::Title, "the title drags the window");
    Canvas pickerCanvas;
    const auto saveView = [&](const AppPickerView& v, AppPickerView::Part hover, int hoverMatch, const std::wstring& name,
                              const std::function<const IconImage*(const InstalledApp&)>& icon) {
        pickerCanvas.Resize(v.Width(), v.Height());
        Canvas framed;
        framed.Resize(v.Width() + 40, v.Height() + 40);
        framed.FillRoundRect(0, 0, float(framed.Width()), float(framed.Height()), 0.0f, {0.12f, 0.14f, 0.22f, 1.0f});
        v.Paint(pickerCanvas, hover, hoverMatch, icon);
        framed.Draw(pickerCanvas, 20, 20);
        SavePng(outDir + L"\\" + name, framed.Bytes(), UINT(framed.Width()), UINT(framed.Height()));
    };
    std::vector<IconImage> fakeIcons;
    const Rgba tints[] = {{0.95f, 0.55f, 0.15f, 1.0f}, {0.15f, 0.55f, 0.95f, 1.0f}, {0.85f, 0.45f, 0.30f, 1.0f},
                          {0.20f, 0.60f, 0.95f, 1.0f}, {0.40f, 0.40f, 0.95f, 1.0f}, {0.25f, 0.80f, 0.45f, 1.0f}};
    for (const Rgba& tint : tints) fakeIcons.push_back(FakeIcon(24, tint));
    const auto fakeIcon = [&](const InstalledApp& a) { return &fakeIcons[a.name.size() % fakeIcons.size()]; };
    view.SetFilter(L"");
    view.MoveSelection(2);
    saveView(view, AppPickerView::Part::Row, 4, L"picker.png", fakeIcon);
    view.SetFilter(L"st");
    saveView(view, AppPickerView::Part::Browse, -1, L"picker_search.png", fakeIcon);

    // ---- This PC, read-only ----
    LARGE_INTEGER frequency, start, end;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&start);
    const std::vector<InstalledApp> installed = ListInstalledApps();
    QueryPerformanceCounter(&end);
    const double listMs = double(end.QuadPart - start.QuadPart) * 1000.0 / double(frequency.QuadPart);
    int withIcons = 0;
    std::vector<IconImage> realIcons(std::min<size_t>(installed.size(), 12));
    for (size_t i = 0; i < realIcons.size(); ++i) withIcons += ShellIcon(installed[i].target, 24, realIcons[i]) ? 1 : 0;
    Log("Start lists %zu apps (read in %.0f ms); the first %zu have %d icons", installed.size(), listMs, realIcons.size(),
        withIcons);
    check(installed.size() > 10, "the app picker finds the apps in Start");
    int exePaths = 0;
    for (const InstalledApp& entry : installed)
        exePaths += ExeFromAppsFolderId(entry.target.substr(wcslen(L"shell:AppsFolder\\"))).empty() ? 0 : 1;
    std::vector<std::wstring> targets;
    for (const InstalledApp& entry : installed) targets.push_back(entry.target);
    DockModel startModel;
    startModel.SetPins(targets);
    startModel.Refresh([](HWND window) {
        DWORD cloaked = 0;
        DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
        return cloaked == 0;
    }, GetForegroundWindow(), {});
    int openNow = 0;
    for (size_t i = 0; i < targets.size() && i < startModel.Apps().size(); ++i)
        openNow += startModel.Apps()[i].windows.empty() ? 0 : 1;
    Log("Of those, %d are named by an exe path and %zu by an app ID; %d have windows open on this desktop right now",
        exePaths, installed.size() - exePaths, openNow);
    check(withIcons == int(realIcons.size()), "apps in the picker show their real icons");
    bool sorted = true;
    for (size_t i = 1; i < installed.size(); ++i) sorted = sorted && _wcsicmp(installed[i - 1].name.c_str(), installed[i].name.c_str()) <= 0;
    check(sorted, "the list is in name order");
    AppPickerView real;
    real.Build(1.0f);
    real.SetApps(&installed);
    saveView(real, AppPickerView::Part::None, -1, L"picker_real.png", [&](const InstalledApp& a) -> const IconImage* {
        for (size_t i = 0; i < realIcons.size(); ++i)
            if (installed[i].target == a.target && !realIcons[i].Empty()) return &realIcons[i];
        return nullptr;
    });

    Log(pass ? "RESULT: PASS" : "RESULT: FAIL");
    if (SUCCEEDED(hr)) CoUninitialize();
    return pass ? 0 : 9;
}
