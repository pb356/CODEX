#pragma once
#include <cstdint>
// Independent policy derived from this application's requirements. No upstream source is included.
namespace playback {
enum class Reason : uint32_t {
    Locked = 1,
    Disconnected = 2,
    Sleeping = 4,
    Hidden = 8,
    Covered = 16,
    Remote = 32,
    SavingPower = 64,
    Failed = 128
};
struct Gate {
    uint32_t reasons = 0;
    void set(Reason reason, bool enabled) {
        auto flag = static_cast<uint32_t>(reason);
        if (enabled)
            reasons |= flag;
        else
            reasons &= ~flag;
    }
    bool paused() const {
        return reasons != 0;
    }
};
enum class Mode { Static, Energy, Standard };
inline unsigned frameInterval(Mode mode, bool reducedMotion, const Gate &gate) {
    if (gate.paused() || reducedMotion || mode == Mode::Static)
        return 0;
    return mode == Mode::Energy ? 67 : 34; // <= 15 and <= 30 fps; no busy loop.
}
} // namespace playback
