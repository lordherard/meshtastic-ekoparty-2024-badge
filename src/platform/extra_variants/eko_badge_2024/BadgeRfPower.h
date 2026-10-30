#pragma once
#include <atomic>
#include <cstdint>

namespace eko
{
constexpr int8_t RF_UNAVAILABLE = -128;
constexpr int8_t RF_BLOCKED = -127;
inline std::atomic<int8_t> effectiveTxPower{RF_UNAVAILABLE};
inline int8_t selectTxPower(int requested, unsigned regionalLimit, int hardwareLimit)
{
    int result = requested ? requested : (regionalLimit ? static_cast<int>(regionalLimit) : 17);
    if (regionalLimit && result > static_cast<int>(regionalLimit))
        result = regionalLimit;
    if (result > hardwareLimit)
        result = hardwareLimit;
    // The normal badge profile never enables the special 20 dBm PA mode.
    if (result > 17)
        result = 17;
    return result < 2 ? RF_BLOCKED : static_cast<int8_t>(result);
}
inline bool txPowerReady()
{
    return effectiveTxPower.load() >= 2;
}
} // namespace eko
