#include "configuration.h"

#ifdef EKO_BADGE_2024
#include "BadgeActivity.h"
#include "BadgeAudioBackend.h"
#include "BadgeBuild.h"
#include "BadgeComposer.h"
#include "BadgeDestinationGuard.h"
#include "BadgeInbox.h"
#include "BadgeMessageSender.h"
#include "BadgeSound.h"
#include "RadioLibInterface.h"
#ifndef EKO_BADGE_HOST_TEST
#include "driver/gpio.h"
#include "esp_timer.h"
#endif
#ifndef EKO_BADGE_HOST_TEST
#include "gps/RTC.h"
#include <esp_heap_caps.h>
#endif
#include "BadgeNotifications.h"
#include "BadgePower.h"
#include "BadgeRfPower.h"
#include "BluetoothStatus.h"
#include "Channels.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "SinglePortModule.h"
#include "Throttle.h"
#include "concurrency/LockGuard.h"
#include "concurrency/OSThread.h"
#include "detect/LoRaRadioType.h"
#include "graphics/Screen.h"
#include "modules/TextMessageModule.h"
#include "target_specific.h"
#include <Adafruit_NeoPixel.h>
#include <Preferences.h>
#include <SSD1306Wire.h>
#include <Wire.h>

namespace
{
constexpr uint8_t keyPins[] = {EKO_UP, EKO_DOWN, EKO_BACK, EKO_SELECT, EKO_MENU};
constexpr const char *menuItems[] = {"Estado",      "Nodos",           "Mensajes", "Enviar",
                                     "Dispositivo", "Apagar pantalla", "Sonido",   "Timeout pantalla",
                                     "Canales",     "Editar rapidos",  "Inicio",   "Mesh Tools"};

struct Key {
    bool raw = false;
    bool stable = false;
    uint32_t changed = 0;
    uint32_t repeatAt = 0;
};

class BadgeUI final : public SinglePortModule, private concurrency::OSThread
{
  public:
    explicit BadgeUI(uint8_t address)
        : SinglePortModule("EkoUI", meshtastic_PortNum_TEXT_MESSAGE_APP), concurrency::OSThread("EkoUI", 20),
          oled(address, -1, -1, GEOMETRY_128_32, I2C_ONE, 400000), pixels(3, EKO_PIXELS, NEO_GRB + NEO_KHZ800)
    {
        for (uint8_t pin : keyPins)
            pinMode(pin, INPUT_PULLUP);
        ready = address && oled.init();
        if (ready) {
            oled.flipScreenVertically(); // The badge mounts the OLED rotated 180
                                         // degrees.
            oled.setFont(ArialMT_Plain_10);
            oled.setTextAlignment(TEXT_ALIGN_LEFT);
        }
        loadSettings();
        loadQuick();
        draftGuard.capture(draftDestination);
        quickGuard.capture(quickDestination);
        rememberChannels();
        power.activity(millis());
        pixels.begin();
        pixels.clear();
        pixels.show();
        if (textMessageModule)
            messageObserver.observe(textMessageModule);
        if (bluetoothStatus)
            bluetoothObserver.observe(&bluetoothStatus->onNewStatus);
        sampleMemory();
        LOG_INFO("EKO UI: OLED=%s, TX test capped at 10 dBm", ready ? "ready" : "not found");
    }

  protected:
    bool wantPacket(const meshtastic_MeshPacket *) override { return false; }

    int32_t runOnce() override
    {
        char outgoing[meshtastic_Constants_DATA_PAYLOAD_LEN + 1] = {};
        eko::Destination destination;
        bool transmit = false;
        int32_t nextRun = 20;
        {
            concurrency::LockGuard guard(&mutex);
            bool released = true;
            for (unsigned i = 0; i < 5; ++i)
                released &= digitalRead(keyPins[i]) != LOW && !keys[i].stable;
            power.released(released);
            notifications.tick(millis(), ready && !dark && !pairing && bannerPage(), released);
            checkChannels();
            for (unsigned i = 0; i < 5; ++i) {
                bool pressed = digitalRead(keyPins[i]) == LOW;
                Key &key = keys[i];
                if (pressed != key.raw) {
                    key.raw = pressed;
                    key.changed = millis();
                }
                if (key.raw != key.stable && Throttle::hasElapsed(key.changed, 35)) {
                    key.stable = key.raw;
                    if (key.stable) {
                        press(i);
                        key.repeatAt = millis() + 600;
                    }
                } else if ((i < 2 || (i == 3 && composer.mode == eko::EditMode::GROUPS && composer.group == 16)) && key.raw &&
                           key.stable && page == EDIT && !pairing && composer.mode != eko::EditMode::CANCEL &&
                           Throttle::deadlinePassed(key.repeatAt)) {
                    press(i);
                    key.repeatAt = millis() + 120;
                }
            }
            checkChannels();
            sampleMemory();
            const bool wasPlaying = audio.busy();
            audio.tick(millis());
            dirty |= wasPlaying != audio.busy();
            nextRun = audio.busy() ? 5 : 20;
            if (pairing || pendingSend)
                power.activity(millis());
            if (!dark && !pairing && !pendingSend && power.expired()) {
                dark = true;
                dirty = true;
            }
            if (pendingSend) {
                transmit = true;
                pendingSend = false;
                destination = pendingDestination;
                snprintf(outgoing, sizeof(outgoing), "%s", pendingText);
            }
            if (dirty || Throttle::hasElapsed(lastDraw, 200)) {
                lastDraw = millis();
                render(dirty || Throttle::hasElapsed(lastStatusDraw, 1000));
            }
        }
        if (transmit) {
            eko::SendReceipt receipt;
            const char *result = eko::sendMessage(destination, outgoing, &receipt, &pendingGuard);
            concurrency::LockGuard guard(&mutex);
            sendResult = result;
            activity.add(receipt.accepted ? eko::ActivityKind::TX_QUEUED : eko::ActivityKind::TX_ERROR, toolsNow(),
                         destination.node, destination.channel);
            if (receipt.accepted) {
                eko::InboxMessage message;
                message.id = receipt.id;
                message.from = nodeDB->getNodeNum();
                message.direct = destination.node != NODENUM_BROADCAST;
                message.peer = destination.node;
                message.channel = destination.channel;
                message.outgoing = true;
                snprintf(message.text, sizeof(message.text), "%s", outgoing);
                const auto token = inbox.add(message, millis());
                if (returnToConversation) {
                    selectedMessage = token;
                    textLine = 0;
                    page = MESSAGES;
                    if (resultIsDraft)
                        composer.clear();
                    returnToConversation = false;
                }
            }
            dirty = true;
            lastDraw = millis() - 200;
        }
        return nextRun;
    }

  private:
    enum Page {
        MENU,
        STATUS,
        NODES,
        MESSAGES,
        SEND,
        INFO,
        CONFIRM,
        SEND_RESULT,
        SEND_KIND,
        DEST_KIND,
        CHANNEL_PICK,
        NODE_PICK,
        DEST_INFO,
        EDIT,
        REVIEW,
        EDIT_CONFIRM,
        MESSAGE_ACTIONS,
        REPLY_DRAFT,
        SOUND_INFO,
        DISPLAY_SETTINGS,
        THREADS,
        CHANNELS_VIEW,
        NODE_INFO,
        QUICK_EDIT,
        QUICK_CONFIRM,
        NODE_SEARCH,
        TOOLS,
        MESH_STATS,
        ACTIVITY,
        DIAGNOSTICS,
        ABOUT
    } page = STATUS;
    SSD1306Wire oled;
    Adafruit_NeoPixel pixels;
    concurrency::Lock mutex;
    Key keys[5];
    eko::BadgeInbox inbox;
    eko::BadgeNotifications notifications;
    uint32_t selectedMessage = 0;
    Page inboxReturn = MENU;
    unsigned returnIndex = 0, returnLine = 0, messageAction = 0;
    bool discardForReply = false;
    const char *replyError = nullptr;
    unsigned selection = 0, index = 0, textLine = 0;
    bool ready = false, dark = false, bleConnected = false, pairing = false;
    uint32_t lastDraw = 0, lastStatusDraw = 0;
    bool dirty = true, screenDark = false;
    uint32_t lastColors[3] = {};
    char lastTop[80] = {}, lastBottom[80] = {};
    eko::BadgePower power;
    eko::BadgeAudioBackend audioBackend;
    eko::BadgeSound<eko::BadgeAudioBackend> audio{audioBackend};
    const char *settingsResult = nullptr;
    unsigned soundAction = 0;

    void loadSettings()
    {
        Preferences prefs;
        if (prefs.begin("eko_energy", true)) {
            const uint32_t value = prefs.getUInt("display_s", 60);
            power.seconds = eko::BadgePower::validTimeout(value) ? value : 60;
            prefs.end();
        }
        if (prefs.begin("eko_notify", true)) {
            audio.setEnabled(prefs.getBool("sound", false));
            prefs.end();
        }
    }

    bool saveSetting(const char *space, const char *key, uint32_t value, bool boolean = false)
    {
        Preferences prefs;
        if (!prefs.begin(space, false))
            return false;
        const bool saved = boolean ? prefs.putBool(key, value) == 1 : prefs.putUInt(key, value) == sizeof(uint32_t);
        prefs.end();
        return saved;
    }

    const char *sendResult = "";
    char passkey[7] = {};
    eko::BadgeComposer<meshtastic_Constants_DATA_PAYLOAD_LEN> composer;
    eko::Destination draftDestination, pendingDestination;
    char pendingText[meshtastic_Constants_DATA_PAYLOAD_LEN + 1] = {};
    bool pendingSend = false, resultIsDraft = false;
    unsigned choice = 0, reviewLine = 0;
    uint32_t candidateNode = 0;
    unsigned candidateChannel = 0;
    const char *destinationMode = "";
    const char *editorError = nullptr;
    CallbackObserver<BadgeUI, const meshtastic_MeshPacket *> messageObserver{this, &BadgeUI::received};
    CallbackObserver<BadgeUI, const meshtastic::Status *> bluetoothObserver{this, &BadgeUI::bluetooth};

#include "BadgeTools.inc"
#include "BadgeUx.inc"

    int received(const meshtastic_MeshPacket *packet)
    {
        if (!packet || packet->which_payload_variant != meshtastic_MeshPacket_decoded_tag ||
            packet->decoded.portnum != meshtastic_PortNum_TEXT_MESSAGE_APP || !packet->from ||
            packet->from == nodeDB->getNodeNum() || !packet->decoded.payload.size ||
            (packet->to != NODENUM_BROADCAST && packet->to != nodeDB->getNodeNum()))
            return 0;
        auto node = nodeDB->getMeshNode(packet->from);
        if (node && nodeInfoLiteIsIgnored(node))
            return 0;
        if (!packet->id) {
            LOG_DEBUG("EKO: text without packet ID from=0x%08x", packet->from);
            return 0;
        }
        if (packet->channel >= channels.getNumChannels())
            return 0;
        const bool muted = isMutedForPacket(*packet);
        const bool wake = !muted && shouldWakeOnReceivedMessage();
        concurrency::LockGuard guard(&mutex);
        checkChannels();
        if (inbox.duplicate(packet->from, packet->id))
            return 0;
        eko::InboxMessage message;
        message.id = packet->id;
        message.from = packet->from;
        message.hasRxRssi = packet->has_rx_rssi && !packet->via_mqtt;
        message.rxRssi = packet->rx_rssi;
        message.direct = packet->to != NODENUM_BROADCAST;
        message.peer = packet->from;
        message.channel = packet->channel;
        size_t length = packet->decoded.payload.size;
        if (length > sizeof(packet->decoded.payload.bytes))
            length = sizeof(packet->decoded.payload.bytes);
        if (length > sizeof(message.text) - 1)
            length = sizeof(message.text) - 1;
        for (size_t i = 0; i < length; ++i) {
            uint8_t c = packet->decoded.payload.bytes[i];
            message.text[i] = c >= 32 && c != 127 ? static_cast<char>(c) : ' ';
        }
        message.text[length] = 0;
        sanitizeUtf8(message.text, sizeof(message.text));
        const uint32_t token = inbox.add(message, millis());
        if (token && !muted)
            activity.add(eko::ActivityKind::RX, toolsNow(), message.from, message.channel);
        if (token && !muted) {
            notifications.received(token, message.direct ? eko::NotificationEvent::DIRECT_MESSAGE
                                                         : eko::NotificationEvent::MESSAGE_RECEIVED);
            audio.received(millis());
            if (wake) {
                dark = false;
                power.activity(millis());
            }
        }
        dirty = true;
        lastDraw = millis() - 200;
        return 0;
    }

    int bluetooth(const meshtastic::Status *status)
    {
        if (status->getStatusType() != STATUS_TYPE_BLUETOOTH)
            return 0;
        auto update = static_cast<const meshtastic::BluetoothStatus *>(status);
        concurrency::LockGuard guard(&mutex);
        pairing = update->getConnectionState() == meshtastic::BluetoothStatus::ConnectionState::PAIRING;
        const bool previousConnected = bleConnected;
        bleConnected = update->getConnectionState() == meshtastic::BluetoothStatus::ConnectionState::CONNECTED;
        if (previousConnected != bleConnected)
            activity.add(bleConnected ? eko::ActivityKind::BLE_UP : eko::ActivityKind::BLE_DOWN, toolsNow());
        if (pairing) {
            snprintf(passkey, sizeof(passkey), "%06lu", strtoul(update->getPasskey().c_str(), nullptr, 10));
            dark = false;
            power.activity(millis());
        }
        dirty = true;
        return 0;
    }

    void press(unsigned key)
    {
        dirty = true;
        if (power.consumeKey(dark, millis())) {
            dark = false;
            return;
        }
        if (pairing) {
            if (key == 2 || key == 4)
                pairing = false;
            return;
        }
        if (notifications.banner) {
            if (!notifications.armed)
                return;
            const uint32_t token = notifications.token;
            if (key == 2 || key == 3 || key == 4) {
                notifications.dismiss();
                if (key == 3)
                    openInbox(token);
            }
            return;
        }
        if (toolsPress(key))
            return;
        if (uxPress(key))
            return;
        if (inboxPress(key))
            return;
        if (composePress(key))
            return;
        if (key == 4 || key == 2) {
            audio.cancel();
            page = MENU;
            index = textLine = 0;
            return;
        }
        if (page == MENU) {
            if (key == 0)
                selection = (selection + 11) % 12;
            if (key == 1)
                selection = (selection + 1) % 12;
            if (key == 3) {
                index = textLine = 0;
                if (selection == 5) {
                    dark = true;

                } else if (selection == 2) {
                    openThreads();
                } else if (selection == 8) {
                    page = CHANNELS_VIEW;
                    candidateChannel = channels.getPrimaryIndex();
                    channelAction = 0;
                } else if (selection == 9) {
                    page = QUICK_EDIT;
                    quickSlot = 0;
                    editingQuick = false;
                } else if (selection == 11) {
                    page = TOOLS;
                    toolPage = 0;
                } else if (selection == 10) {
                    page = STATUS;
                    index = 0;
                } else if (selection == 6) {
                    page = SOUND_INFO;
                    soundAction = 0;
                    settingsResult = nullptr;
                } else if (selection == 7) {
                    page = DISPLAY_SETTINGS;
                    settingsResult = nullptr;
                } else {
                    constexpr Page pages[] = {STATUS, NODES, MESSAGES, SEND_KIND, INFO};
                    page = pages[selection];
                }
            }
        } else if (page == SOUND_INFO) {
            if (key == 0 || key == 1) {
                soundAction ^= 1;
                settingsResult = nullptr;
            }
            if (key == 3) {
                if (soundAction == 1) {
                    settingsResult = audio.test(millis()) ? "Reproduciendo" : "Espera / error";
                } else {
                    const bool value = !audio.isEnabled();
                    if (!value)
                        audio.setEnabled(false);
                    const bool saved = saveSetting("eko_notify", "sound", value, true);
                    if (saved)
                        audio.setEnabled(value);
                    settingsResult = saved ? "Guardado" : "No guardado";
                }
            }
        } else if (page == DISPLAY_SETTINGS) {
            if (key == 3) {
                const uint32_t value = power.seconds == 15 ? 30 : power.seconds == 30 ? 60 : power.seconds == 60 ? 0 : 15;
                const bool saved = saveSetting("eko_energy", "display_s", value);
                if (saved)
                    power.seconds = value;
                settingsResult = saved ? "Guardado" : "No guardado";
            }
        } else if (page == CONFIRM) {
            if (key == 3)
                sendPreset();
        } else if (page == SEND_RESULT) {
            if (key == 3)
                page = resultIsDraft ? EDIT : SEND;
        } else {
            unsigned limit = page == NODES ? nodeDB->getNumMeshNodes() : page == SEND ? 6 : page == STATUS ? 5 : 3;
            if (!limit)
                limit = 1;
            if (key == 0)
                index = (index + limit - 1) % limit;
            if (key == 1)
                index = (index + 1) % limit;
            if (key == 3 && page == SEND) {
                page = CONFIRM;
            }
        }
    }

    bool bannerPage() const { return page == MENU || page == STATUS || page == NODES || page == INFO || page == SOUND_INFO; }

    void messageLabel(const eko::InboxMessage *message, char *out, size_t capacity)
    {
        if (!message) {
            snprintf(out, capacity, "%s", inbox.size() ? "Ya no disponible" : "Sin mensajes");
            return;
        }
        char name[32];
        auto node = nodeDB->getMeshNode(message->from);
        if (message->outgoing)
            snprintf(name, sizeof(name), "Yo/cola");
        else if (node && node->short_name[0])
            snprintf(name, sizeof(name), "%.4s", node->short_name);
        else
            snprintf(name, sizeof(name), "!%08x", message->from);
        if (message->direct)
            snprintf(out, capacity, "%s DM", name);
        else
            snprintf(out, capacity, "%s Ch%u", name, message->channel);
    }

    void openInbox(uint32_t token = 0)
    {
        fromThreads = false;
        inboxReturn = page;
        returnIndex = index;
        returnLine = textLine;
        selectedMessage = token ? token : inbox.size() ? inbox.newest()->token : 0;
        notifications.dismiss();
        page = MESSAGES;
        textLine = 0;
        replyError = nullptr;
    }

    void closeInbox()
    {
        page = inboxReturn;
        index = returnIndex;
        textLine = returnLine;
        replyError = nullptr;
    }

    void startReply(bool discard)
    {
        const auto message = inbox.find(selectedMessage);
        if (!message) {
            replyError = "Ya no disponible";
            return;
        }
        eko::Destination destination{message->direct ? message->peer : NODENUM_BROADCAST, message->channel};
        const auto plan = eko::planMessage(destination);
        if (plan.error) {
            replyError = plan.error;
            return;
        }
        if (composer.length && !discard) {
            discardForReply = false;
            page = REPLY_DRAFT;
            return;
        }
        composer.clear();
        draftDestination = destination;
        draftGuard.capture(destination);
        returnToConversation = true;
        refreshPlan();
        page = EDIT;
        notifications.dismiss();
    }

    bool inboxPress(unsigned key)
    {
        if (page == MESSAGES) {
            if (key == 2 || key == 4)
                closeInbox();
            else if (key == 0 && textLine)
                --textLine;
            else if (key == 1 && textLine < 240)
                ++textLine;
            else if (key == 3) {
                page = MESSAGE_ACTIONS;
                messageAction = 0;
                replyError = nullptr;
            }
            return true;
        }
        if (page == MESSAGE_ACTIONS) {
            if (key < 2) {
                messageAction = (messageAction + (key == 0 ? 4 : 1)) % 5;
                replyError = nullptr;
            }
            if (key == 2 || key == 4 || (key == 3 && messageAction == 4))
                page = MESSAGES;
            else if (key == 3 && messageAction == 0)
                startReply(false);
            else if (key == 3 && messageAction == 3) {
                auto m = inbox.find(selectedMessage);
                if (m) {
                    quickDestination = {m->direct ? m->peer : NODENUM_BROADCAST, m->channel};
                    quickGuard.capture(quickDestination);
                    quickInThread = true;
                    index = 0;
                    page = SEND;
                }
            } else if (key == 3) {
                selectedMessage = inbox.next(selectedMessage, true, messageAction == 1);
                textLine = 0;
                page = MESSAGES;
            }
            return true;
        }
        if (page == REPLY_DRAFT) {
            if (key < 2)
                discardForReply = !discardForReply;
            if (key == 2 || key == 4)
                page = MESSAGE_ACTIONS;
            else if (key == 3) {
                if (discardForReply)
                    startReply(true);
                else {
                    page = EDIT;
                    notifications.dismiss();
                }
            }
            return true;
        }
        return false;
    }

    void queueText(const eko::Destination &destination, const char *text, bool draft)
    {
        if (pendingSend)
            return;
        const auto &guard = draft ? draftGuard : quickGuard;
        const auto plan = eko::planMessage(destination);
        if (plan.error || !guard.matches(destination)) {
            sendResult = plan.error ? plan.error : "Config cambio: reelegir";
            page = SEND_RESULT;
            resultIsDraft = draft;
            return;
        }
        pendingGuard = guard;
        pendingDestination = destination;
        snprintf(pendingText, sizeof(pendingText), "%s", text);
        pendingSend = true;
        resultIsDraft = draft;
        page = SEND_RESULT;
        sendResult = "Enviando...";
    }

    void sendPreset()
    {
        returnToConversation = quickInThread;
        queueText(quickDestination, quickReplies[index % 6], false);
    }

    void moveChannel(int direction)
    {
        unsigned total = channels.getNumChannels();
        for (unsigned n = 0; n < total; ++n) {
            candidateChannel = (candidateChannel + total + direction) % total;
            if (eko::channelEnabled(candidateChannel))
                return;
        }
    }

    void moveNode(int direction)
    {
        unsigned total = nodeDB->getNumMeshNodes(), position = 0;
        for (unsigned n = 0; n < total; ++n) {
            auto node = nodeDB->getMeshNodeByIndex(n);
            if (node && node->num == candidateNode)
                position = n;
        }
        candidateNode = 0;
        for (unsigned n = 0; n < total; ++n) {
            position = (position + total + direction) % total;
            auto node = nodeDB->getMeshNodeByIndex(position);
            if (node && node->num != nodeDB->getNodeNum() && !nodeInfoLiteIsIgnored(node) &&
                !(nodeInfoLiteHasIsUnmessagable(node) && nodeInfoLiteIsUnmessagable(node))) {
                candidateNode = node->num;
                return;
            }
        }
    }

    void refreshPlan()
    {
        auto plan = eko::planMessage(draftDestination);
        composer.limit = plan.error ? meshtastic_Constants_DATA_PAYLOAD_LEN : plan.limit;
        destinationMode = plan.mode;
        editorError = draftGuard.matches(draftDestination) ? plan.error : "Config cambio: reelegir";
    }

    bool composePress(unsigned key)
    {
        if (page == EDIT) {
            editorError = nullptr;
            auto effect = composer.press(key);
            if (effect == eko::EditEffect::INBOX) {
                openInbox();
            } else if (effect == eko::EditEffect::DESTINATION) {
                page = DEST_KIND;
                choice = 0;
            } else if (effect == eko::EditEffect::REVIEW) {
                refreshPlan();
                if (!composer.length)
                    editorError = "Texto vacio";
                else if (composer.length > composer.limit)
                    editorError = "Texto largo: acortar";
                if (!editorError) {
                    page = REVIEW;
                    reviewLine = 0;
                }
            } else if (effect == eko::EditEffect::DISCARDED) {
                page = SEND_KIND;
                choice = 1;
            }
            return true;
        }
        if (page == REVIEW || page == EDIT_CONFIRM) {
            if (key == 2 || key == 4) {
                page = EDIT;
                return true;
            }
            if (page == REVIEW) {
                if (key == 0 && reviewLine)
                    --reviewLine;
                if (key == 1 && reviewLine < composer.length)
                    ++reviewLine;
                if (key == 3)
                    page = EDIT_CONFIRM;
            } else if (key == 3) {
                queueText(draftDestination, composer.text, true);
            }
            return true;
        }
        if (page == SEND_KIND || page == DEST_KIND) {
            if (key < 2)
                choice = (choice + 1) % 2;
            if (key == 2 || key == 4) {
                page = page == SEND_KIND ? MENU : EDIT;
            } else if (key == 3) {
                if (page == SEND_KIND) {
                    page = choice == 0 ? SEND : DEST_KIND;
                    choice = index = 0;
                    quickDestination = {NODENUM_BROADCAST, channels.getPrimaryIndex()};
                    quickGuard.capture(quickDestination);
                    quickInThread = false;
                } else if (choice == 0) {
                    page = CHANNEL_PICK;
                    candidateChannel = channels.getPrimaryIndex();
                    if (!eko::channelEnabled(candidateChannel))
                        moveChannel(1);
                } else {
                    page = NODE_PICK;
                    candidateNode = nodeDB->getNodeNum();
                    moveNode(1);
                }
            }
            return true;
        }
        if (page == CHANNEL_PICK || page == NODE_PICK) {
            if (key == 2 || key == 4) {
                page = DEST_KIND;
                return true;
            }
            if (key < 2) {
                if (page == CHANNEL_PICK)
                    moveChannel(key == 0 ? -1 : 1);
                else
                    moveNode(key == 0 ? -1 : 1);
            }
            if (key == 3) {
                if (page == CHANNEL_PICK && eko::channelEnabled(candidateChannel))
                    draftDestination = {NODENUM_BROADCAST, static_cast<uint8_t>(candidateChannel)};
                else if (page == NODE_PICK && candidateNode)
                    draftDestination = {candidateNode, channels.getPrimaryIndex()};
                else
                    return true;
                draftGuard.capture(draftDestination);
                quickGuard.capture(quickDestination);
                returnToConversation = false;
                refreshPlan();
                page = DEST_INFO;
            }
            return true;
        }
        if (page == DEST_INFO) {
            if (key == 2 || key == 4)
                page = DEST_KIND;
            if (key == 3) {
                page = EDIT;
                composer.mode = eko::EditMode::GROUPS;
            }
            return true;
        }
        return false;
    }

    void destinationLabel(char *out, size_t capacity)
    {
        if (draftDestination.node == NODENUM_BROADCAST && draftDestination.channel < channels.getNumChannels())
            snprintf(out, capacity, "Ch%u %s", draftDestination.channel, channels.getName(draftDestination.channel));
        else
            snprintf(out, capacity, "DM !%08x", draftDestination.node);
    }

    template <size_t N> void editWindow(const eko::BadgeComposer<N> &editor, char *out, size_t capacity)
    {
        const unsigned width = inbox.unreadCount() ? 92 : 116;
        size_t start = 0;
        for (;;) {
            size_t bytes = editor.cursor - start;
            if (bytes + 3 < capacity) {
                size_t n = 0;
                if (start)
                    out[n++] = '<';
                memcpy(out + n, editor.text + start, bytes);
                n += bytes;
                out[n++] = '|';
                out[n] = 0;
                if (oled.getStringWidth(out) <= width)
                    break;
            }
            if (start == editor.cursor) {
                snprintf(out, capacity, "|");
                break;
            }
            start = eko::nextChar(editor.text, start);
        }
        size_t n = strlen(out), end = editor.cursor;
        while (end < editor.length) {
            size_t next = eko::nextChar(editor.text, end), bytes = next - end;
            if (n + bytes + 2 >= capacity)
                break;
            memcpy(out + n, editor.text + end, bytes);
            out[n + bytes] = 0;
            if (oled.getStringWidth(out) > width) {
                out[n] = 0;
                break;
            }
            n += bytes;
            end = next;
        }
        if (end < editor.length)
            out[n++] = '>';
        out[n] = 0;
    }

    void editorWindow(char *out, size_t capacity) { editWindow(composer, out, capacity); }

    bool composeRender(char *top, size_t topSize, char *bottom, size_t bottomSize)
    {
        if (page == SEND_KIND || page == DEST_KIND) {
            snprintf(top, topSize, "%s", page == SEND_KIND ? "Enviar" : "Destino");
            snprintf(bottom, bottomSize, "> %s",
                     page == SEND_KIND ? (choice ? "Escribir / borrador" : "Rapidos") : (choice ? "Nodo conocido" : "Canal"));
        } else if (page == CHANNEL_PICK) {
            snprintf(top, topSize, "Elegir canal >");
            if (eko::channelEnabled(candidateChannel))
                snprintf(bottom, bottomSize, "Ch%u %s", candidateChannel, channels.getName(candidateChannel));
            else
                snprintf(bottom, bottomSize, "Sin canales activos");
        } else if (page == NODE_PICK) {
            auto node = candidateNode ? nodeDB->getMeshNode(candidateNode) : nullptr;
            snprintf(top, topSize, node ? "!%08x" : "Sin nodos elegibles", candidateNode);
            snprintf(bottom, bottomSize, "%s", node ? node->short_name : "<: volver");
        } else if (page == DEST_INFO || page == EDIT_CONFIRM) {
            destinationLabel(top, topSize);
            if (page == EDIT_CONFIRM)
                snprintf(bottom, bottomSize, "%s > ENVIAR", destinationMode);
            else
                snprintf(bottom, bottomSize, "%s", editorError ? editorError : destinationMode);
        } else if (page == REVIEW) {
            destinationLabel(top, topSize);
            unsigned saved = textLine;
            textLine = reviewLine;
            messageLine(composer.text, bottom, bottomSize);
            reviewLine = textLine;
            textLine = saved;
        } else if (page == EDIT) {
            editorWindow(top, topSize);
            if (editorError)
                snprintf(bottom, bottomSize, "%s", editorError);
            else if (composer.mode == eko::EditMode::ACTIONS)
                snprintf(bottom, bottomSize, "> %s", eko::ACTIONS[composer.action]);
            else if (composer.mode == eko::EditMode::CURSOR)
                snprintf(bottom, bottomSize, "^< v> / >: aceptar");
            else if (composer.mode == eko::EditMode::CANCEL)
                snprintf(bottom, bottomSize, "%s", composer.discard ? "> Descartar" : "> Seguir editando");
            else if (composer.full)
                snprintf(bottom, bottomSize, "Limite %u B", static_cast<unsigned>(composer.limit));
            else if (composer.mode == eko::EditMode::CHARACTERS)
                snprintf(bottom, bottomSize, "[%.*s] %u/%u B",
                         static_cast<int>(eko::utf8Bytes(eko::charAt(eko::GROUPS[composer.group], composer.character))),
                         eko::charAt(eko::GROUPS[composer.group], composer.character), static_cast<unsigned>(composer.length),
                         static_cast<unsigned>(composer.limit));
            else
                snprintf(bottom, bottomSize, "%s %u/%u B", eko::GROUPS[composer.group], static_cast<unsigned>(composer.length),
                         static_cast<unsigned>(composer.limit));
        } else {
            return false;
        }
        return true;
    }

    void trimLine(char *text, unsigned width)
    {
        sanitizeUtf8(text, strlen(text) + 1);
        size_t length = strlen(text);
        while (length && oled.getStringWidth(text) > width) {
            --length;
            while (length && (static_cast<uint8_t>(text[length]) & 0xc0) == 0x80)
                --length;
            text[length] = 0;
        }
    }

    void line(int y, const char *text)
    {
        char fitted[80];
        snprintf(fitted, sizeof(fitted), "%s", text);
        trimLine(fitted, 128);
        oled.drawString(0, y, fitted);
    }

    void messageLine(const char *text, char *out, size_t capacity)
    {
        unsigned row = 0;
        const char *cursor = text;
        do {
            size_t n = 0;
            out[0] = '\0';
            while (*cursor && n + 1 < capacity) {
                size_t bytes = 1;
                while (cursor[bytes] && (static_cast<uint8_t>(cursor[bytes]) & 0xc0) == 0x80)
                    ++bytes;
                if (n + bytes >= capacity)
                    break;
                memcpy(out + n, cursor, bytes);
                out[n + bytes] = '\0';
                if (oled.getStringWidth(out) > 128) {
                    out[n] = '\0';
                    break;
                }
                cursor += bytes;
                n += bytes;
            }
            if (row == textLine || !*cursor) {
                textLine = row;
                return;
            }
            ++row;
        } while (*cursor);
    }

    void render(bool refresh = true)
    {
        uint32_t notification = notifications.ledOn(millis()) ? pixels.Color(0, 16, 16) : 0;
        // D2 is below the OLED; D3 and D4 illuminate the glasses.
        uint32_t colors[] = {radioType == RF95_RADIO ? pixels.Color(0, 2, bleConnected ? 2 : 0) : pixels.Color(2, 0, 0),
                             notification, notification};
        bool colorsChanged = false;
        for (unsigned i = 0; i < 3; ++i) {
            const uint32_t color = dark && i == 0 ? 0 : colors[i];
            if (lastColors[i] != color) {
                lastColors[i] = color;
                pixels.setPixelColor(i, color);
                colorsChanged = true;
            }
        }
        if (colorsChanged)
            pixels.show();
        if (!ready) {
            dirty = false;
            return;
        }
        if (screenDark != dark) {
            screenDark = dark;
            if (dark)
                oled.displayOff();
            else
                oled.displayOn();
            dirty = true;
        }
        if (dark) {
            dirty = false;
            return;
        }
        if (toolsPage() && !dirty && !pairing && !notifications.banner && !toolsOverlay)
            return;
        if (!dirty && !refresh)
            return;
        lastStatusDraw = millis();
        toolsOverlay = pairing || notifications.banner;
        dirty = false;
        oled.clear();
        char top[80] = {}, bottom[80] = {};
        eko::InboxMessage *readMessage = nullptr;
        if (pairing) {
            snprintf(top, sizeof(top), "BLE PIN: %s", passkey);
            snprintf(bottom, sizeof(bottom), "Confirmar en telefono");
        } else if (notifications.banner) {
            const auto message = inbox.find(notifications.token);
            const unsigned card = ((millis() - notifications.bannerStarted) / 2000) % 3;
            if (card == 0) {
                snprintf(top, sizeof(top), "%u nuevos / %u conv.", inbox.unreadCount(), inbox.threads(nullptr, true));
                messageLabel(message, bottom, sizeof(bottom));
            } else if (card == 1) {
                messageLabel(message, top, sizeof(top));
                snprintf(bottom, sizeof(bottom), "%s", message ? message->text : "Ya no disponible");
            } else {
                snprintf(top, sizeof(top), "Mensajes (%u)", inbox.unreadCount());
                snprintf(bottom, sizeof(bottom), ">: abrir  <: volver");
            }
        } else if (toolsRender(top, sizeof(top), bottom, sizeof(bottom))) {
        } else if (uxRender(top, sizeof(top), bottom, sizeof(bottom))) {
        } else if (composeRender(top, sizeof(top), bottom, sizeof(bottom))) {
        } else if (page == MENU) {
            snprintf(top, sizeof(top), "Menu %u/12", selection + 1);
            snprintf(bottom, sizeof(bottom), "> %s", menuItems[selection]);
            if (selection == 2)
                snprintf(bottom, sizeof(bottom), "> Mensajes (%u)", inbox.unreadCount());
        } else if (page == STATUS) {
            if (index % 5 == 0) {
                snprintf(top, sizeof(top), "%.12s BLE:%s", owner.short_name[0] ? owner.short_name : "MeshBadge",
                         bleConnected ? "ON" : "--");
                snprintf(bottom, sizeof(bottom), "%u nuevos / Ch #%u", inbox.unreadCount(), channels.getPrimaryIndex());
            } else if (index % 5 == 1) {
                snprintf(top, sizeof(top), "LoRa %s / TX %s", radioType == RF95_RADIO ? "OK" : "ERROR",
                         config.lora.tx_enabled ? "ON" : "OFF");
                snprintf(bottom, sizeof(bottom), "BLE %s",
                         bleConnected               ? "conectado"
                         : config.bluetooth.enabled ? "sin conexion"
                                                    : "apagado");
            } else if (index % 5 == 3) {
                snprintf(top, sizeof(top), "Heap vacio: %lu", static_cast<unsigned long>(heapEmpty));
                if (measuredFull)
                    snprintf(bottom, sizeof(bottom), "Heap 20: %lu", static_cast<unsigned long>(heapFull));
                else
                    snprintf(bottom, sizeof(bottom), "Heap 20: pendiente");
            } else if (index % 5 == 4) {
                snprintf(top, sizeof(top), "Historial RAM %u/20", inbox.size());
                snprintf(bottom, sizeof(bottom), "Se borra al reiniciar");
            } else {
                const int effective = eko::effectiveTxPower.load();
                if (effective >= 2)
                    snprintf(top, sizeof(top), "TX: %d dBm (sol:%d)", effective, config.lora.tx_power);
                else
                    snprintf(top, sizeof(top), "%s", effective == eko::RF_BLOCKED ? "TX bloqueado: potencia" : "TX: N/D");
                snprintf(bottom, sizeof(bottom), "Region: %u", static_cast<unsigned>(config.lora.region));
            }
        } else if (page == NODES) {
            unsigned total = nodeDB->getNumMeshNodes();
            auto node = total ? nodeDB->getMeshNodeByIndex(index % total) : nullptr;
            if (node) {
                snprintf(top, sizeof(top), "%u/%u !%08x", index % total + 1, total, node->num);
                if (nodeInfoLiteHasSnr(node))
                    snprintf(bottom, sizeof(bottom), "%s SNR %.1f", node->short_name, node->snr);
                else
                    snprintf(bottom, sizeof(bottom), "%s SNR N/D", node->short_name);
            } else {
                snprintf(top, sizeof(top), "Sin nodos conocidos");
            }
        } else if (page == MESSAGES) {
            readMessage = inbox.find(selectedMessage);
            messageLabel(readMessage, top, sizeof(top));
            if (readMessage)
                messageLine(readMessage->text, bottom, sizeof(bottom));
            else
                snprintf(bottom, sizeof(bottom), "%s", inbox.size() ? ">: acciones / <: volver" : "<: volver");
        } else if (page == MESSAGE_ACTIONS) {
            constexpr const char *actions[] = {"Responder", "Anterior (viejo)", "Siguiente (nuevo)", "Respuesta rapida",
                                               "Volver"};
            snprintf(top, sizeof(top), "%s",
                     replyError      ? replyError
                     : inbox.limited ? "Historial limitado"
                                     : "Mensaje / acciones");
            snprintf(bottom, sizeof(bottom), "> %s", actions[messageAction]);
        } else if (page == REPLY_DRAFT) {
            snprintf(top, sizeof(top), "%s", replyError ? replyError : "Ya hay un borrador");
            snprintf(bottom, sizeof(bottom), "%s", discardForReply ? "> Descartar y responder" : "> Volver al borrador");
        } else if (page == SOUND_INFO) {
            snprintf(top, sizeof(top), "%sSound: %s%s", soundAction == 0 ? "> " : "", audio.isEnabled() ? "ON" : "OFF",
                     audio.hasFailed() ? " Error" : "");
            snprintf(bottom, sizeof(bottom), "%s",
                     settingsResult && strcmp(settingsResult, "No guardado") == 0 ? settingsResult
                     : audio.busy()                                               ? "Test Sound / sonando"
                     : soundAction == 1                                           ? "> Test Sound"
                                                                                  : "  Test Sound");
        } else if (page == DISPLAY_SETTINGS) {
            if (power.seconds)
                snprintf(top, sizeof(top), "Pantalla: %lu s", static_cast<unsigned long>(power.seconds));
            else
                snprintf(top, sizeof(top), "Pantalla: Always On");
            snprintf(bottom, sizeof(bottom), "%s", settingsResult ? settingsResult : ">: cambiar");
        } else if (page == SEND) {
            if (quickDestination.node == NODENUM_BROADCAST)
                snprintf(top, sizeof(top), "Rapido %u/6 Ch%u", index % 6 + 1, quickDestination.channel);
            else
                snprintf(top, sizeof(top), "R%u/6 @%08x", index % 6 + 1, quickDestination.node);
            snprintf(bottom, sizeof(bottom), "%s", quickReplies[index % 6]);
        } else if (page == CONFIRM) {
            if (quickDestination.node == NODENUM_BROADCAST)
                snprintf(top, sizeof(top), "ENVIAR Ch%u >", quickDestination.channel);
            else
                snprintf(top, sizeof(top), "ENVIAR @%08x", quickDestination.node);
            snprintf(bottom, sizeof(bottom), "%s", quickReplies[index % 6]);
        } else if (page == SEND_RESULT) {
            snprintf(top, sizeof(top), "%s", sendResult);
            snprintf(bottom, sizeof(bottom), ">: volver / <: menu");
        } else {
            constexpr const char *titles[] = {"EKOPARTY 2024", "OLED SSD1306 128x32", "BOOT: menu / <: atras"};
            constexpr const char *details[] = {"ESP32-C6 / flash 4MB", "RFM95 / TX max 10dBm", "^ v: mover / >: elegir"};
            snprintf(top, sizeof(top), "%s", titles[index % 3]);
            snprintf(bottom, sizeof(bottom), "%s", details[index % 3]);
        }
        if (!pairing && !notifications.banner && inbox.unreadCount()) {
            char counter[8];
            snprintf(counter, sizeof(counter), " [%u]", inbox.unreadCount());
            const unsigned width = 128 - oled.getStringWidth(counter);
            trimLine(top, width);
            strncat(top, counter, sizeof(top) - strlen(top) - 1);
        }
        if (readMessage)
            readMessage->unread = false;
        if (strcmp(top, lastTop) == 0 && strcmp(bottom, lastBottom) == 0)
            return;
        snprintf(lastTop, sizeof(lastTop), "%s", top);
        snprintf(lastBottom, sizeof(lastBottom), "%s", bottom);
        line(0, top);
        line(16, bottom);
        oled.display();
    }
};
} // namespace

void earlyInitVariant()
{
#if EKO_AUDIO_VERIFIED
#ifndef EKO_BADGE_HOST_TEST
    // Preload HIGH before enabling output to avoid an active-low shutdown glitch.
    gpio_set_level(static_cast<gpio_num_t>(EKO_AUDIO_SD), 1);
    gpio_set_direction(static_cast<gpio_num_t>(EKO_AUDIO_SD), GPIO_MODE_OUTPUT);
#endif
    pinMode(EKO_AUDIO_SD, OUTPUT);
    digitalWrite(EKO_AUDIO_SD, HIGH);
#else
    // Leave unverified amplifier shutdown and PWM pins high impedance.
    pinMode(EKO_AUDIO_SD, INPUT);
#endif
    pinMode(EKO_AUDIO_PWM, INPUT);
}

void lateInitVariant()
{
    uint8_t address = 0;
    for (uint8_t candidate : {0x3c, 0x3d}) {
        Wire.beginTransmission(candidate);
        if (Wire.endTransmission() == 0) {
            address = candidate;
            break;
        }
    }
#ifndef EKO_BADGE_HOST_TEST
    LOG_INFO("EKO_ALLOC before_ui=%lu", static_cast<unsigned long>(heap_caps_get_free_size(MALLOC_CAP_8BIT)));
#endif
    new BadgeUI(address);
    // The excluded power FSM normally starts BLE when entering the powered state.
    setBluetoothEnable(true);
}
#endif
