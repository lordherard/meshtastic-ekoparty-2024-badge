#pragma once
#include "Throttle.h"
#include <cstdint>
namespace eko
{
// Requests coalesce into one active sequence; nothing is queued behind it.
template <class Backend> class BadgeSound
{
  public:
    explicit BadgeSound(Backend &b) : driver(b) {}
    bool available() const { return driver.available() && !failed; }
    bool hasFailed() const { return failed; }
    bool isEnabled() const { return enabled; }
    bool busy() const { return state != State::IDLE; }
    void setEnabled(bool value)
    {
        enabled = value && available();
        if (!enabled)
            cancel();
    }
    bool received(uint32_t now) { return enabled && request(now); }
    bool test(uint32_t now) { return request(now); }
    void cancel()
    {
        if (busy())
            driver.stop();
        state = State::IDLE;
    }
    void tick(uint32_t now)
    {
        switch (state) {
        case State::REQUEST:
            driver.setDuty(8); // Physically tested L4, never exceed 8/1024.
            if (!driver.prepare()) {
                fail();
                break;
            }
            changed = now;
            state = State::PREPARE;
            break;
        case State::PREPARE:
            if (Throttle::hasElapsed(changed, 100))
                tone(1300, State::FIRST, now);
            break;
        case State::FIRST:
            if (Throttle::hasElapsed(changed, 70))
                quiet(State::GAP1, now);
            break;
        case State::GAP1:
            if (Throttle::hasElapsed(changed, 25))
                tone(1600, State::SECOND, now);
            break;
        case State::SECOND:
            if (Throttle::hasElapsed(changed, 70))
                quiet(State::GAP2, now);
            break;
        case State::GAP2:
            if (Throttle::hasElapsed(changed, 25))
                tone(2000, State::THIRD, now);
            break;
        case State::THIRD:
            if (Throttle::hasElapsed(changed, 70))
                cancel();
            break;
        case State::IDLE:
            break;
        }
    }

  private:
    enum class State { IDLE, REQUEST, PREPARE, FIRST, GAP1, SECOND, GAP2, THIRD };
    Backend &driver;
    State state = State::IDLE;
    uint32_t changed = 0, lastStart = 0;
    bool enabled = false, started = false, failed = false;
    bool request(uint32_t now)
    {
        if (!available() || busy() || (started && !Throttle::hasElapsed(lastStart, 2000)))
            return false;
        started = true;
        lastStart = now;
        state = State::REQUEST;
        return true;
    }
    void fail()
    {
        driver.stop();
        state = State::IDLE;
        enabled = false;
        failed = true;
    }
    void tone(uint32_t hz, State next, uint32_t now)
    {
        if (!driver.tone(hz)) {
            fail();
            return;
        }
        changed = now;
        state = next;
    }
    void quiet(State next, uint32_t now)
    {
        if (!driver.quiet()) {
            fail();
            return;
        }
        changed = now;
        state = next;
    }
};
} // namespace eko
