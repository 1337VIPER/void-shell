#include "audio.h"

#include <mmdeviceapi.h>

#include <algorithm>

bool MasterVolume::Connect() {
    endpoint_.Reset();
    ComPtr<IMMDeviceEnumerator> devices;
    CHECK_HR(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&devices)));
    ComPtr<IMMDevice> device;
    if (FAILED(devices->GetDefaultAudioEndpoint(eRender, eConsole, &device))) return false;  // no speakers
    CHECK_HR(device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr,
                              reinterpret_cast<void**>(endpoint_.ReleaseAndGetAddressOf())));
    return true;
}

bool MasterVolume::Read(float& level, bool& muted) {
    // The default device can change (headphones plugged in), so a failed read reconnects once.
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (!endpoint_ && !Connect()) return false;
        BOOL mute = FALSE;
        if (SUCCEEDED(endpoint_->GetMasterVolumeLevelScalar(&level)) && SUCCEEDED(endpoint_->GetMute(&mute))) {
            muted = mute != FALSE;
            return true;
        }
        endpoint_.Reset();
    }
    return false;
}

bool MasterVolume::SetLevel(float level) {
    if (!endpoint_ && !Connect()) return false;
    level = std::clamp(level, 0.0f, 1.0f);
    if (level > 0.0f) endpoint_->SetMute(FALSE, nullptr);
    return SUCCEEDED(endpoint_->SetMasterVolumeLevelScalar(level, nullptr));
}

bool MasterVolume::ToggleMute() {
    float level = 0.0f;
    bool muted = false;
    if (!Read(level, muted)) return false;
    return SUCCEEDED(endpoint_->SetMute(muted ? FALSE : TRUE, nullptr));
}
