#pragma once
#include <cstdint>

namespace eko
{
enum class ActivityKind : uint8_t { RX, TX_QUEUED, TX_ERROR, BLE_UP, BLE_DOWN };
struct ActivityEvent {
    uint64_t atMs = 0;
    uint32_t node = 0;
    ActivityKind kind = ActivityKind::RX;
    uint8_t channel = 0;
    uint8_t reserved[2] = {};
};
static_assert(sizeof(ActivityEvent) == 16, "Activity event budget");
class BadgeActivity
{
  public:
    static constexpr unsigned CAPACITY = 16;
    void add(ActivityKind kind, uint64_t atMs, uint32_t node = 0, uint8_t channel = 0)
    {
        events[next] = {atMs, node, kind, channel, {0, 0}};
        next = (next + 1) % CAPACITY;
        if (count < CAPACITY)
            ++count;
    }
    unsigned size() const { return count; }
    const ActivityEvent *newest(unsigned offset = 0) const
    {
        return offset < count ? &events[(next + CAPACITY - 1 - offset) % CAPACITY] : nullptr;
    }

  private:
    ActivityEvent events[CAPACITY] = {};
    uint8_t next = 0, count = 0;
};
inline bool recentlyHeard(uint32_t heard, uint32_t now)
{
    return heard > 1600000000u && now >= heard && now - heard < 7200u;
}
} // namespace eko
