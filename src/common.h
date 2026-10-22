#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wrl/client.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <DirectXMath.h>

#include <cstdint>

using Microsoft::WRL::ComPtr;

// Log lines go to the debugger and, once LogOpen has been called, to a file.
void LogOpen(const wchar_t* path);
void Log(const char* format, ...);

// Logs a failed HRESULT with its call site and returns false from the calling function.
#define CHECK_HR(expr)                                                                       \
    do {                                                                                     \
        HRESULT hrCheck_ = (expr);                                                           \
        if (FAILED(hrCheck_)) {                                                              \
            Log("%s failed: 0x%08X (%s:%d)", #expr, (unsigned)hrCheck_, __FILE__, __LINE__); \
            return false;                                                                    \
        }                                                                                    \
    } while (0)
