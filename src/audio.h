#pragma once

#include <endpointvolume.h>

#include "common.h"

// The default playback device's master volume. Needs COM. Reconnects if the default device changes.
class MasterVolume {
public:
    bool Read(float& level, bool& muted);
    bool SetLevel(float level);  // 0..1
    bool ToggleMute();

private:
    bool Connect();
    ComPtr<IAudioEndpointVolume> endpoint_;
};
