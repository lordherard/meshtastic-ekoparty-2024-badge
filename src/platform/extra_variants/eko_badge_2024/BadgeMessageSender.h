#pragma once
#include "configuration.h"
#ifdef EKO_BADGE_2024
#include "mesh/generated/meshtastic/mesh.pb.h"

namespace eko
{
struct Destination {
    uint32_t node = 0xffffffff;
    uint8_t channel = 0;
};
struct SendPlan {
    const char *error = nullptr;
    const char *mode = "";
    size_t limit = 0;
    bool pki = false;
};
bool channelEnabled(unsigned channel);
SendPlan planMessage(const Destination &destination);
struct SendReceipt {
    bool accepted = false;
    uint32_t id = 0;
};
struct DestinationGuard;
const char *sendMessage(const Destination &destination, const char *text, SendReceipt *receipt = nullptr,
                        const DestinationGuard *guard = nullptr);
} // namespace eko
#endif
