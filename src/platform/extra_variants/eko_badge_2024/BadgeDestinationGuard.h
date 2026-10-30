#pragma once
#include "BadgeMessageSender.h"
#include "Channels.h"
#include "NodeDB.h"
#include <cstring>

namespace eko
{
struct DestinationGuard {
    Destination destination;
    meshtastic_Channel channel = {}, primary = {};
    uint8_t primaryIndex = 0;
    meshtastic_Config_LoRaConfig lora = {};
    meshtastic_NodeInfoLite_public_key_t peerKey = {};
    bool licensed = false, valid = false;
    void capture(Destination value)
    {
        valid = channelEnabled(value.channel);
        destination = value;
        if (!valid)
            return;
        memcpy(&channel, &channels.getByIndex(value.channel), sizeof(channel));
        primaryIndex = channels.getPrimaryIndex();
        memcpy(&primary, &channels.getByIndex(primaryIndex), sizeof(primary));
        memcpy(&lora, &config.lora, sizeof(lora));
        licensed = owner.is_licensed;
        peerKey = {};
        if (value.node != NODENUM_BROADCAST && !licensed)
            valid = nodeDB->copyPublicKey(value.node, peerKey);
    }
    bool matches(Destination value) const
    {
        if (!valid || value.node != destination.node || value.channel != destination.channel || !channelEnabled(value.channel) ||
            primaryIndex != channels.getPrimaryIndex() || memcmp(&primary, &channels.getByIndex(primaryIndex), sizeof(primary)) ||
            licensed != owner.is_licensed || memcmp(&lora, &config.lora, sizeof(lora)) ||
            memcmp(&channel, &channels.getByIndex(value.channel), sizeof(channel)))
            return false;
        if (value.node != NODENUM_BROADCAST && !licensed) {
            meshtastic_NodeInfoLite_public_key_t key = {};
            if (!nodeDB->copyPublicKey(value.node, key) || key.size != peerKey.size || memcmp(key.bytes, peerKey.bytes, key.size))
                return false;
        }
        return true;
    }
};
} // namespace eko
