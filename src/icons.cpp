#include "icons.h"

#include <shlobj.h>
#include <shobjidl.h>

#include <algorithm>
#include <cwchar>

namespace {

// Reads a 32bpp bitmap and scales it (area average) into a size x size premultiplied image.
bool FromBitmap(HBITMAP bitmap, int size, IconImage& out) {
    BITMAP info = {};
    if (!GetObjectW(bitmap, sizeof(info), &info) || info.bmWidth <= 0 || info.bmHeight == 0) return false;
    const int width = info.bmWidth;
    const int height = abs(info.bmHeight);
    BITMAPINFO header = {};
    header.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    header.bmiHeader.biWidth = width;
    header.bmiHeader.biHeight = -height;  // top-down
    header.bmiHeader.biPlanes = 1;
    header.bmiHeader.biBitCount = 32;
    header.bmiHeader.biCompression = BI_RGB;
    std::vector<uint32_t> source(size_t(width) * height);
    HDC dc = GetDC(nullptr);
    const int rows = GetDIBits(dc, bitmap, 0, UINT(height), source.data(), &header, DIB_RGB_COLORS);
    ReleaseDC(nullptr, dc);
    if (rows != height) return false;

    // Some sources hand back straight alpha, some premultiplied; colour brighter than alpha means straight.
    bool straight = false;
    for (const uint32_t p : source) {
        const uint32_t a = p >> 24;
        if (((p >> 16) & 255) > a || ((p >> 8) & 255) > a || (p & 255) > a) {
            straight = true;
            break;
        }
    }
    out.size = size;
    out.pixels.assign(size_t(size) * size, 0);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const int x0 = x * width / size, x1 = std::max(x0 + 1, (x + 1) * width / size);
            const int y0 = y * height / size, y1 = std::max(y0 + 1, (y + 1) * height / size);
            uint32_t sum[4] = {};
            int count = 0;
            for (int sy = y0; sy < y1; ++sy) {
                for (int sx = x0; sx < x1; ++sx) {
                    uint32_t p = source[size_t(sy) * width + sx];
                    if (straight) {
                        const uint32_t a = p >> 24;
                        p = (a << 24) | ((((p >> 16) & 255) * a / 255) << 16) | ((((p >> 8) & 255) * a / 255) << 8) |
                            ((p & 255) * a / 255);
                    }
                    for (int c = 0; c < 4; ++c) sum[c] += (p >> (c * 8)) & 255;
                    ++count;
                }
            }
            out.pixels[size_t(y) * size + x] = ((sum[3] / count) << 24) | ((sum[2] / count) << 16) |
                                               ((sum[1] / count) << 8) | (sum[0] / count);
        }
    }
    return true;
}

// Draws an icon on black and on white and recovers alpha from the difference: works for every icon format.
bool FromIcon(HICON icon, int size, IconImage& out) {
    BITMAPINFO header = {};
    header.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    header.bmiHeader.biWidth = size;
    header.bmiHeader.biHeight = -size;
    header.bmiHeader.biPlanes = 1;
    header.bmiHeader.biBitCount = 32;
    header.bmiHeader.biCompression = BI_RGB;
    HDC dc = CreateCompatibleDC(nullptr);
    void* blackBits = nullptr;
    void* whiteBits = nullptr;
    HBITMAP black = CreateDIBSection(dc, &header, DIB_RGB_COLORS, &blackBits, nullptr, 0);
    HBITMAP white = CreateDIBSection(dc, &header, DIB_RGB_COLORS, &whiteBits, nullptr, 0);
    bool ok = black && white;
    if (ok) {
        memset(blackBits, 0x00, size_t(size) * size * 4);
        memset(whiteBits, 0xFF, size_t(size) * size * 4);
        HGDIOBJ previous = SelectObject(dc, black);
        DrawIconEx(dc, 0, 0, icon, size, size, 0, nullptr, DI_NORMAL);
        SelectObject(dc, white);
        DrawIconEx(dc, 0, 0, icon, size, size, 0, nullptr, DI_NORMAL);
        SelectObject(dc, previous);
        GdiFlush();
        const auto* b = static_cast<const uint32_t*>(blackBits);
        const auto* w = static_cast<const uint32_t*>(whiteBits);
        out.size = size;
        out.pixels.assign(size_t(size) * size, 0);
        for (size_t i = 0; i < out.pixels.size(); ++i) {
            // On black the colour is c*a; on white it is c*a + (1-a); so a = 1 - (white - black).
            const int diff = int((w[i] >> 8) & 255) - int((b[i] >> 8) & 255);
            const uint32_t a = uint32_t(std::clamp(255 - diff, 0, 255));
            out.pixels[i] = (a << 24) | (b[i] & 0x00FFFFFFu);
        }
    }
    if (black) DeleteObject(black);
    if (white) DeleteObject(white);
    DeleteDC(dc);
    return ok;
}

}  // namespace

bool ShellIcon(const std::wstring& parsingName, int size, IconImage& out) {
    if (parsingName.empty()) return false;
    ComPtr<IShellItemImageFactory> factory;
    if (FAILED(SHCreateItemFromParsingName(parsingName.c_str(), nullptr, IID_PPV_ARGS(&factory)))) return false;
    HBITMAP bitmap = nullptr;
    if (FAILED(factory->GetImage({size, size}, SIIGBF_ICONONLY | SIIGBF_BIGGERSIZEOK, &bitmap)) || !bitmap)
        return false;
    const bool ok = FromBitmap(bitmap, size, out);
    DeleteObject(bitmap);
    return ok;
}

bool WindowIcon(HWND window, int size, IconImage& out) {
    DWORD_PTR result = 0;
    HICON icon = nullptr;
    for (const WPARAM kind : {WPARAM(ICON_BIG), WPARAM(ICON_SMALL2), WPARAM(ICON_SMALL)}) {
        if (SendMessageTimeoutW(window, WM_GETICON, kind, 0, SMTO_ABORTIFHUNG, 60, &result) && result) {
            icon = reinterpret_cast<HICON>(result);
            break;
        }
    }
    if (!icon) icon = reinterpret_cast<HICON>(GetClassLongPtrW(window, GCLP_HICON));
    if (!icon) icon = reinterpret_cast<HICON>(GetClassLongPtrW(window, GCLP_HICONSM));
    return icon && FromIcon(icon, size, out);
}

std::wstring AppNameFromExe(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    std::wstring name = slash == std::wstring::npos ? path : path.substr(slash + 1);
    const size_t dot = name.find_last_of(L'.');
    if (dot != std::wstring::npos) name = name.substr(0, dot);

    DWORD ignored = 0;
    const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &ignored);
    if (!size) return name;
    std::vector<uint8_t> data(size);
    if (!GetFileVersionInfoW(path.c_str(), 0, size, data.data())) return name;
    struct Translation {
        WORD language, codePage;
    }* translations = nullptr;
    UINT length = 0;
    if (!VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&translations), &length) ||
        length < sizeof(Translation))
        return name;
    wchar_t query[64];
    swprintf_s(query, L"\\StringFileInfo\\%04x%04x\\FileDescription", translations[0].language, translations[0].codePage);
    wchar_t* description = nullptr;
    if (VerQueryValueW(data.data(), query, reinterpret_cast<void**>(&description), &length) && length > 1 && description)
        return description;
    return name;
}
