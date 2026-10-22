#include "wallpaper.h"

#include <wincodec.h>

#include <algorithm>

std::wstring GlobalWallpaperPath() {
    wchar_t path[MAX_PATH * 2] = {};
    if (!SystemParametersInfoW(SPI_GETDESKWALLPAPER, ARRAYSIZE(path), path, 0)) return L"";
    return path;
}

bool LoadImageFill(const std::wstring& path, UINT width, UINT height, std::vector<uint8_t>& bgra) {
    if (path.empty() || width == 0 || height == 0) return false;
    ComPtr<IWICImagingFactory> factory;
    CHECK_HR(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)));
    // Decoders are chosen by content, so extensionless files like Windows' TranscodedWallpaper load too.
    ComPtr<IWICBitmapDecoder> decoder;
    CHECK_HR(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand,
                                                &decoder));
    ComPtr<IWICBitmapFrameDecode> frame;
    CHECK_HR(decoder->GetFrame(0, &frame));
    UINT sourceWidth = 0;
    UINT sourceHeight = 0;
    CHECK_HR(frame->GetSize(&sourceWidth, &sourceHeight));
    if (sourceWidth == 0 || sourceHeight == 0) return false;

    // Crop the middle of the image to the target's shape, then scale.
    const double targetAspect = double(width) / height;
    const double sourceAspect = double(sourceWidth) / sourceHeight;
    WICRect crop = {0, 0, int(sourceWidth), int(sourceHeight)};
    if (sourceAspect > targetAspect) {
        crop.Width = std::max(1, int(sourceHeight * targetAspect + 0.5));
        crop.X = (int(sourceWidth) - crop.Width) / 2;
    } else {
        crop.Height = std::max(1, int(sourceWidth / targetAspect + 0.5));
        crop.Y = (int(sourceHeight) - crop.Height) / 2;
    }
    ComPtr<IWICBitmapClipper> clipper;
    CHECK_HR(factory->CreateBitmapClipper(&clipper));
    CHECK_HR(clipper->Initialize(frame.Get(), &crop));
    ComPtr<IWICBitmapScaler> scaler;
    CHECK_HR(factory->CreateBitmapScaler(&scaler));
    CHECK_HR(scaler->Initialize(clipper.Get(), width, height, WICBitmapInterpolationModeFant));
    ComPtr<IWICFormatConverter> converter;
    CHECK_HR(factory->CreateFormatConverter(&converter));
    CHECK_HR(converter->Initialize(scaler.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.0,
                                   WICBitmapPaletteTypeCustom));

    bgra.resize(size_t(width) * height * 4);
    CHECK_HR(converter->CopyPixels(nullptr, width * 4, UINT(bgra.size()), bgra.data()));
    for (size_t i = 3; i < bgra.size(); i += 4) bgra[i] = 255;
    return true;
}

void FillSolid(std::vector<uint8_t>& bgra, UINT width, UINT height, COLORREF color) {
    bgra.resize(size_t(width) * height * 4);
    for (size_t i = 0; i < bgra.size(); i += 4) {
        bgra[i + 0] = GetBValue(color);
        bgra[i + 1] = GetGValue(color);
        bgra[i + 2] = GetRValue(color);
        bgra[i + 3] = 255;
    }
}
