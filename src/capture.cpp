#include "capture.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

bool CreateBgraTexture(ID3D11Device* device, UINT width, UINT height, const void* pixels, UINT pitch,
                       ComPtr<ID3D11Texture2D>& out) {
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA init = {pixels, pitch, 0};
    CHECK_HR(device->CreateTexture2D(&desc, pixels ? &init : nullptr, &out));
    return true;
}

bool CaptureWithDuplication(ID3D11Device* device, ID3D11DeviceContext* context, HMONITOR monitor,
                            ComPtr<ID3D11Texture2D>& out) {
    // Duplication only works on an output of the adapter the device was created on.
    ComPtr<IDXGIDevice> dxgiDevice;
    CHECK_HR(device->QueryInterface(IID_PPV_ARGS(&dxgiDevice)));
    ComPtr<IDXGIAdapter> adapter;
    CHECK_HR(dxgiDevice->GetAdapter(&adapter));

    ComPtr<IDXGIOutput> output;
    for (UINT i = 0;; ++i) {
        ComPtr<IDXGIOutput> candidate;
        if (adapter->EnumOutputs(i, &candidate) == DXGI_ERROR_NOT_FOUND) break;
        DXGI_OUTPUT_DESC desc;
        if (SUCCEEDED(candidate->GetDesc(&desc)) && desc.Monitor == monitor) {
            output = candidate;
            break;
        }
    }
    if (!output) {
        Log("Duplication: this monitor is not driven by the render adapter");
        return false;
    }

    ComPtr<IDXGIOutput1> output1;
    CHECK_HR(output.As(&output1));
    ComPtr<IDXGIOutputDuplication> duplication;
    CHECK_HR(output1->DuplicateOutput(device, &duplication));

    DXGI_OUTDUPL_DESC duplicationDesc;
    duplication->GetDesc(&duplicationDesc);
    if (duplicationDesc.Rotation != DXGI_MODE_ROTATION_IDENTITY &&
        duplicationDesc.Rotation != DXGI_MODE_ROTATION_UNSPECIFIED) {
        Log("Duplication: rotated monitor, leaving it to GDI");
        return false;
    }

    // The first frame after DuplicateOutput normally holds the whole desktop. Some drivers hand back an
    // empty first frame, so keep trying briefly for one that came from a real present.
    bool haveFrame = false;
    for (int attempt = 0; attempt < 8; ++attempt) {
        DXGI_OUTDUPL_FRAME_INFO info;
        ComPtr<IDXGIResource> resource;
        HRESULT hr = duplication->AcquireNextFrame(attempt == 0 ? 60 : 30, &info, &resource);
        if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
            if (haveFrame) break;
            continue;
        }
        CHECK_HR(hr);

        ComPtr<ID3D11Texture2D> frame;
        hr = resource.As(&frame);
        if (SUCCEEDED(hr)) {
            D3D11_TEXTURE2D_DESC frameDesc;
            frame->GetDesc(&frameDesc);
            if (frameDesc.Format != DXGI_FORMAT_B8G8R8A8_UNORM) {
                Log("Duplication: unexpected format %d, leaving it to GDI", int(frameDesc.Format));
                hr = E_FAIL;
            } else if (out || CreateBgraTexture(device, frameDesc.Width, frameDesc.Height, nullptr, 0, out)) {
                context->CopyResource(out.Get(), frame.Get());
                haveFrame = true;
            } else {
                hr = E_FAIL;
            }
        }
        duplication->ReleaseFrame();
        if (FAILED(hr)) {
            out.Reset();
            return false;
        }
        if (haveFrame && info.LastPresentTime.QuadPart != 0) break;
    }
    if (!haveFrame) {
        Log("Duplication: no frame arrived");
        return false;
    }
    return true;
}

bool CaptureWithGdi(ID3D11Device* device, HMONITOR monitor, ComPtr<ID3D11Texture2D>& out) {
    MONITORINFO info = {sizeof(info)};
    if (!GetMonitorInfoW(monitor, &info)) return false;
    const RECT rect = info.rcMonitor;
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;

    BITMAPINFO bitmapInfo = {};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = width;
    bitmapInfo.bmiHeader.biHeight = -height;  // top-down
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;

    HDC screen = GetDC(nullptr);
    HDC memory = CreateCompatibleDC(screen);
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(screen, &bitmapInfo, DIB_RGB_COLORS, &bits, nullptr, 0);
    bool ok = false;
    if (bitmap) {
        HGDIOBJ previous = SelectObject(memory, bitmap);
        if (BitBlt(memory, 0, 0, width, height, screen, rect.left, rect.top, SRCCOPY | CAPTUREBLT)) {
            GdiFlush();
            auto* pixels = static_cast<uint32_t*>(bits);
            for (size_t i = 0, n = size_t(width) * height; i < n; ++i) pixels[i] |= 0xFF000000u;  // GDI leaves alpha 0
            ok = CreateBgraTexture(device, UINT(width), UINT(height), bits, UINT(width) * 4, out);
        }
        SelectObject(memory, previous);
        DeleteObject(bitmap);
    }
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    return ok;
}

}  // namespace

ComPtr<IDXGIAdapter1> FindAdapterForMonitor(HMONITOR monitor) {
    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return nullptr;
    for (UINT a = 0;; ++a) {
        ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(a, &adapter) == DXGI_ERROR_NOT_FOUND) break;
        for (UINT o = 0;; ++o) {
            ComPtr<IDXGIOutput> output;
            if (adapter->EnumOutputs(o, &output) == DXGI_ERROR_NOT_FOUND) break;
            DXGI_OUTPUT_DESC desc;
            if (SUCCEEDED(output->GetDesc(&desc)) && desc.Monitor == monitor) return adapter;
        }
    }
    return nullptr;
}

bool CaptureMonitor(ID3D11Device* device, ID3D11DeviceContext* context, HMONITOR monitor,
                    ComPtr<ID3D11Texture2D>& outTexture, const char** outMethod) {
    outTexture.Reset();
    if (CaptureWithDuplication(device, context, monitor, outTexture)) {
        *outMethod = "desktop duplication";
        return true;
    }
    outTexture.Reset();
    if (CaptureWithGdi(device, monitor, outTexture)) {
        *outMethod = "GDI";
        return true;
    }
    *outMethod = "none";
    return false;
}

bool CreateTestPattern(ID3D11Device* device, UINT width, UINT height, int variant,
                       ComPtr<ID3D11Texture2D>& outTexture) {
    std::vector<uint32_t> pixels(size_t(width) * height);
    const auto pack = [](float r, float g, float b) {
        const auto c = [](float v) { return uint32_t(std::clamp(v, 0.0f, 255.0f)); };
        return 0xFF000000u | (c(r) << 16) | (c(g) << 8) | c(b);
    };
    // Odd variants mirror the windows and rotate the wallpaper's colour channels.
    const bool mirrored = (variant & 1) != 0;
    const auto wallpaper = [&](float r, float g, float b) {
        switch (variant % 3) {
            case 1: return pack(g, b, r);
            case 2: return pack(b, r, g);
            default: return pack(r, g, b);
        }
    };
    const auto fill = [&](int x0, int y0, int x1, int y1, uint32_t color) {
        x0 = std::max(x0, 0);
        y0 = std::max(y0, 0);
        x1 = std::min(x1, int(width));
        y1 = std::min(y1, int(height));
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x) pixels[size_t(y) * width + x] = color;
    };

    // Wallpaper: a dusk gradient with a soft glow.
    for (UINT y = 0; y < height; ++y) {
        for (UINT x = 0; x < width; ++x) {
            const float fx = float(x) / width;
            const float fy = float(y) / height;
            const float glow = expf(-((fx - 0.68f) * (fx - 0.68f) * 7.0f + (fy - 0.32f) * (fy - 0.32f) * 11.0f));
            pixels[size_t(y) * width + x] =
                wallpaper(18 + 70 * fy + 150 * glow, 26 + 30 * fy + 80 * glow, 60 + 70 * (1 - fy) + 90 * glow);
        }
    }

    const int unit = std::max(4, int(height) / 54);

    // Taskbar with a row of app icons.
    const int bar = unit * 2 + unit / 2;
    fill(0, int(height) - bar, int(width), int(height), pack(28, 30, 38));
    const uint32_t iconColors[] = {pack(60, 140, 255), pack(255, 120, 60), pack(90, 200, 120), pack(240, 200, 60),
                                   pack(200, 90, 220), pack(230, 230, 235), pack(70, 200, 210)};
    for (int i = 0; i < 7; ++i) {
        const int cx = int(width) / 2 + (i - 3) * bar;
        fill(cx - unit / 2 - 2, int(height) - bar + unit / 2, cx + unit / 2 + 2, int(height) - unit / 2,
             iconColors[i]);
    }

    // Two app windows with title bars and lines of "text".
    const auto window = [&](int x, int y, int w, int h, uint32_t accent) {
        if (mirrored) x = int(width) - x - w;
        fill(x - 1, y - 1, x + w + 1, y + h + 1, pack(70, 74, 86));
        fill(x, y, x + w, y + h, pack(242, 243, 246));
        fill(x, y, x + w, y + unit * 2, pack(222, 225, 232));
        fill(x + unit, y + unit / 2, x + unit * 2, y + unit * 3 / 2, accent);
        const int lines = std::max(0, (h / unit - 4) / 2);
        for (int line = 0; line < lines; ++line) {
            const int ly = y + unit * 3 + line * unit * 2;
            const int lw = (w - unit * 4) * (55 + (line * 37) % 40) / 100;
            fill(x + unit * 2, ly, x + unit * 2 + lw, ly + unit / 2, pack(188, 192, 204));
        }
    };
    window(int(width * 0.08f), int(height * 0.1f), int(width * 0.46f), int(height * 0.62f), iconColors[0]);
    window(int(width * 0.5f), int(height * 0.3f), int(width * 0.4f), int(height * 0.5f), iconColors[1]);

    // Colour bars across the second window.
    for (int i = 0; i < 7; ++i) {
        int x0 = int(width * 0.5f) + unit + i * unit * 3;
        if (mirrored) x0 = int(width) - x0 - unit * 3;
        fill(x0, int(height * 0.3f) + unit * 3, x0 + unit * 3, int(height * 0.3f) + unit * 6, iconColors[i]);
    }

    // A 1-pixel checkerboard: any blur or half-pixel shift in the seamless check shows up here.
    const int checkX = mirrored ? int(width * 0.30f) : int(width * 0.56f);
    const int checkY = int(height * 0.48f);
    const int checkSize = unit * 5;
    for (int y = 0; y < checkSize; ++y)
        for (int x = 0; x < checkSize; ++x)
            pixels[size_t(checkY + y) * width + checkX + x] = ((x + y) & 1) ? pack(255, 255, 255) : pack(0, 0, 0);

    return CreateBgraTexture(device, width, height, pixels.data(), width * 4, outTexture);
}
