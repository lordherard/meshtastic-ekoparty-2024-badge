#pragma once
#include "Throttle.h"
#include <cstdint>

namespace eko
{
enum class NotificationEvent { MESSAGE_RECEIVED, DIRECT_MESSAGE, SYSTEM_ALERT };
class BadgeNotifications
{
  public:
    uint32_t token = 0, bannerStarted = 0;
    bool banner = false, armed = false;

    void received(uint32_t value, NotificationEvent event = NotificationEvent::MESSAGE_RECEIVED)
    {
        if (event == NotificationEvent::SYSTEM_ALERT)
            return;
        token = value;
        pendingBanner = true;
        pendingFlash = true;
    }
    void dismiss() { banner = pendingBanner = armed = false; }
    void tick(uint32_t now, bool canShow, bool released)
    {
        if (flash && Throttle::hasElapsed(flashStarted, 1200))
            flash = false;
        if (pendingFlash && (!flashed || Throttle::hasElapsed(flashStarted, 2000))) {
            flash = flashed = true;
            flashStarted = now;
            pendingFlash = false;
        }
        if (banner && !canShow) {
            banner = armed = false;
            pendingBanner = true;
        }
        if (banner && Throttle::hasElapsed(bannerStarted, 6000))
            dismiss();
        if (pendingBanner && canShow && !banner) {
            banner = true;
            pendingBanner = false;
            armed = false;
            bannerStarted = now;
        } else if (banner && released) {
            armed = true;
        }
    }
    bool ledOn(uint32_t now) const { return flash && ((now - flashStarted) / 200) % 2 == 0; }

  private:
    uint32_t flashStarted = 0;
    bool pendingBanner = false, pendingFlash = false, flash = false, flashed = false;
};
} // namespace eko
