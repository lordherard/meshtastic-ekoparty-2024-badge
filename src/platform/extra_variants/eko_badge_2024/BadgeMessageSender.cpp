#include "configuration.h"
#ifdef EKO_BADGE_2024
#include "BadgeDestinationGuard.h"
#include "BadgeMessageSender.h"
#include "BadgeRfPower.h"
#include "Channels.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "RadioInterface.h"
#include "Router.h"
#include "detect/LoRaRadioType.h"
#include <pb_encode.h>

namespace eko
{
bool channelEnabled(unsigned channel)
{
    return channel < channels.getNumChannels() && channels.getByIndex(channel).has_settings &&
           channels.getByIndex(channel).role != meshtastic_Channel_Role_DISABLED;
}

SendPlan planMessage(const Destination &destination)
{
    SendPlan plan;
    const bool direct = destination.node != NODENUM_BROADCAST;
    if (!channelEnabled(destination.channel)) {
        plan.error = "Canal no disponible";
        return plan;
    }
    if (direct) {
        if (!destination.node || destination.node == nodeDB->getNodeNum() || !nodeDB->getMeshNode(destination.node)) {
            plan.error = "Nodo no disponible";
            return plan;
        }
        if (!owner.is_licensed) {
            meshtastic_NodeInfoLite_public_key_t key = {};
            if (config.security.private_key.size != 32 || !nodeDB->copyPublicKey(destination.node, key) || key.size != 32) {
                plan.error = "DM: faltan claves";
                return plan;
            }
            plan.pki = true;
        }
    }
    plan.mode = plan.pki                                      ? "PKI"
                : owner.is_licensed                           ? "Sin cifrar"
                : channels.usesPublicKey(destination.channel) ? "Canal publico"
                                                              : "Clave de canal";
    meshtastic_Data data = meshtastic_Data_init_zero;
    data.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    data.dest = destination.node;
    data.has_bitfield = true;
    data.bitfield = config.lora.config_ok_to_mqtt << BITFIELD_OK_TO_MQTT_SHIFT;
    // Reserve signing space even when Router could choose an unsigned large
    // packet.
#if !(MESHTASTIC_EXCLUDE_PKI) && !(MESHTASTIC_EXCLUDE_XEDDSA)
    if (!plan.pki)
        data.xeddsa_signature.size = XEDDSA_SIGNATURE_SIZE;
#endif
    const size_t overhead = plan.pki                                      ? MESHTASTIC_PKC_OVERHEAD
                            : channels.isAEADEnabled(destination.channel) ? MESHTASTIC_AEAD_OVERHEAD
                                                                          : 0;
    for (size_t n = 1; n <= meshtastic_Constants_DATA_PAYLOAD_LEN; ++n) {
        data.payload.size = n;
        size_t encoded = 0;
        if (!pb_get_encoded_size(&encoded, &meshtastic_Data_msg, &data) ||
            encoded + MESHTASTIC_HEADER_LENGTH + overhead > MAX_LORA_PAYLOAD_LEN)
            break;
        plan.limit = n;
    }
    return plan;
}

const char *sendMessage(const Destination &destination, const char *text, SendReceipt *receipt, const DestinationGuard *guard)
{
    if (receipt)
        *receipt = {};
#ifdef LORA_DISABLE_SENDING
    return "TX deshabilitado";
#else
    if (!config.lora.tx_enabled || config.lora.region == meshtastic_Config_LoRaConfig_RegionCode_UNSET || radioType != RF95_RADIO)
        return "Revisar config LoRa";
    if (!txPowerReady())
        return "TX: potencia no valida";
    auto plan = planMessage(destination);
    if (plan.error)
        return plan.error;
    const size_t length = strlen(text);
    if (!length)
        return "Texto vacio";
    if (length > plan.limit)
        return "Texto largo: acortar";
    if (guard && !guard->matches(destination))
        return "Config cambio: reelegir";
    auto packet = router->allocForSending();
    if (!packet)
        return "Sin memoria";
    packet->to = destination.node;
    packet->channel = plan.pki ? 0 : destination.channel;
    packet->pki_encrypted = plan.pki;
    packet->want_ack = destination.node != NODENUM_BROADCAST;
    packet->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    packet->decoded.dest = destination.node;
    packet->decoded.payload.size = length;
    memcpy(packet->decoded.payload.bytes, text, length);
    // Copy identity before MeshService takes ownership on every result.
    const uint32_t id = packet->id;
    const bool accepted = service->sendToMesh(packet, RX_SRC_LOCAL, true) == ERRNO_OK;
    if (receipt)
        *receipt = {accepted, id};
    return accepted ? "En cola; sin confirm." : "Error de envio";
#endif
}
} // namespace eko
#endif
