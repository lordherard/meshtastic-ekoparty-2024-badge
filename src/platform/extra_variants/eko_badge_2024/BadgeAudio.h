#pragma once
#include "Throttle.h"
#include <cstdint>

namespace eko
{
enum class AudioEvent { CHANNEL_MESSAGE, DIRECT_MESSAGE, SYSTEM_ALERT, LOW_BATTERY };

template <class Backend> class BadgeAudio
{
  public:
    explicit BadgeAudio(Backend &driver) : driver(driver) {}
    bool available() const { return driver.available() && !failed; }
    bool isEnabled() const { return enabled; }
    bool hasFailed() const { return failed; }
    bool busy() const { return state != State::IDLE; }
    void setEnabled(bool value)
    {
        enabled = value && available();
        if (!enabled) {
            pending = false;
            if (busy())
                driver.stop();
            state = State::IDLE;
        }
    }
    void received(AudioEvent event)
    {
        if (!enabled || (event != AudioEvent::CHANNEL_MESSAGE && event != AudioEvent::DIRECT_MESSAGE))
            return;
        pendingEvent = event;
        pending = true;
    }
    void tick(uint32_t now)
    {
        if (!enabled)
            return;
        if (state == State::IDLE) {
            if (!pending || (started && !Throttle::hasElapsed(lastStart, 2000)))
                return;
            current = pendingEvent;
            pending = false;
            if (!driver.prepare()) {
                fail();
                return;
            }
            started = true;
            lastStart = changed = now;
            state = State::PREPARE;
        } else if (state == State::PREPARE && Throttle::hasElapsed(changed, 100)) {
            tone(1000, State::FIRST, now);
        } else if (state == State::FIRST && Throttle::hasElapsed(changed, 80)) {
            if (!driver.quiet()) {
                fail();
                return;
            }
            state = State::GAP;
            changed = now;
        } else if (state == State::GAP && Throttle::hasElapsed(changed, 100)) {
            tone(current == AudioEvent::DIRECT_MESSAGE ? 1500 : 1000, State::SECOND, now);
        } else if (state == State::SECOND && Throttle::hasElapsed(changed, 80)) {
            driver.stop();
            state = State::IDLE;
        }
    }

  private:
    enum class State { IDLE, PREPARE, FIRST, GAP, SECOND } state = State::IDLE;
    Backend &driver;
    AudioEvent current = AudioEvent::CHANNEL_MESSAGE, pendingEvent = AudioEvent::CHANNEL_MESSAGE;
    uint32_t changed = 0, lastStart = 0;
    bool enabled = false, pending = false, started = false, failed = false;
    void fail()
    {
        failed = true;
        driver.stop();
        state = State::IDLE;
        setEnabled(false);
    }
    void tone(uint32_t frequency, State next, uint32_t now)
    {
        if (!driver.tone(frequency)) {
            fail();
            return;
        }
        state = next;
        changed = now;
    }
};
} // namespace eko
