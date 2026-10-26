#pragma once
#include <cmath>
#include <cstdint>
namespace foliage {
struct Clock {
    uint64_t elapsed = 0, checkpoint = 0;
    bool running = false;
    void setRunning(bool next, uint64_t now) {
        if (running && now >= checkpoint)
            elapsed += now - checkpoint;
        checkpoint = now;
        running = next;
    }
    double seconds(uint64_t now) const {
        return double(elapsed + (running && now >= checkpoint ? now - checkpoint : 0)) / 1000.0;
    }
};
inline float angle(unsigned leaf, double seconds, float strength = 1) {
    constexpr double tau = 6.283185307179586;
    constexpr double periods[]{11.8, 9.4, 13.2}, phases[]{0.2, 2.1, 4.3};
    auto n = leaf % 3;
    return float((1.35 * std::sin(seconds * tau / periods[n] + phases[n]) +
                  .25 * std::sin(seconds * tau / 29.0 + phases[n])) *
                 strength);
}
} // namespace foliage
