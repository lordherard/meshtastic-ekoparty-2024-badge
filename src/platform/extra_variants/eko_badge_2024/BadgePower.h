#pragma once
#include "Throttle.h"
#include <cstdint>

namespace eko
{
class BadgePower
{
  public:
    static bool validTimeout(uint32_t value) { return value == 0 || value == 15 || value == 30 || value == 60; }
    uint32_t seconds = 60;
    void activity(uint32_t now) { lastActivity = now; }
    bool expired() const { return seconds && Throttle::hasElapsed(lastActivity, seconds * 1000); }
    bool consumeKey(bool dark, uint32_t now)
    {
        activity(now);
        if (dark)
            waitRelease = true;
        return waitRelease;
    }
    void released(bool allReleased)
    {
        if (allReleased)
            waitRelease = false;
    }

  private:
    uint32_t lastActivity = 0;
    bool waitRelease = false;
};
} // namespace eko
