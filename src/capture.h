#pragma once

#include "common.h"

// The DXGI adapter that drives a monitor, or null if none matches.
ComPtr<IDXGIAdapter1> FindAdapterForMonitor(HMONITOR monitor);

// Grabs the current image of one monitor into a BGRA8 texture on `device`.
// Tries the Desktop Duplication API first (fast, stays on the GPU), then falls back to GDI.
bool CaptureMonitor(ID3D11Device* device, ID3D11DeviceContext* context, HMONITOR monitor,
                    ComPtr<ID3D11Texture2D>& outTexture, const char** outMethod);

// Builds a fake desktop image for headless tests, so they never need the real screen. Each variant has its own
// wallpaper colours and window layout.
bool CreateTestPattern(ID3D11Device* device, UINT width, UINT height, int variant,
                       ComPtr<ID3D11Texture2D>& outTexture);
