// Exercises the actual badge editor, UI event handlers and sender against fake
// hardware/services. The contracts are bounded editing, explicit confirmation,
// RX isolation and queue ownership. Regressions guarded: received text changes
// a pending message, held RIGHT sends twice, disabled destinations transmit, an
// overlength packet is silently truncated, or a send holds the UI mutex. Nanopb
// descriptors/encoder and OLED font metrics are the production versions.
#ifdef EKO_BADGE_HOST_TEST
#include "BadgeAudio.h"
#include "BadgeComposer.h"
#include "BadgeMessageSender.h"
#include "BadgeRfPower.h"
#include "Fakes.h"
#include "Throttle.h"
#define private public
#define protected public
#include "../../src/platform/extra_variants/eko_badge_2024/variant.cpp"
#undef protected
#undef private
#include <pb_encode.h>
#include <random>
#include <vector>

bool Throttle::isWithinTimespanMs(uint32_t last, uint32_t interval)
{
    return uint32_t(fakeMillis - last) < interval;
}
bool Throttle::deadlinePassed(uint32_t deadline)
{
    return deadlinePassedAt(fakeMillis, deadline);
}

void setup()
{
    for (int &pin : pinState)
        pin = 1;
    fakeLedcFail = false;
    fakeDuty = fakeHz = fakeAttaches = 0;
    fakePrefs.clear();
    fakePrefBytes.clear();
    fakePrefsFail = false;
    fakePrefsWrites = fakeAudioWrites = 0;
    eko::effectiveTxPower = 10;
    fakeMuted = false;
    fakeWake = true;
    config = {};
    config.lora.tx_enabled = true;
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_ANZ;
    config.security.private_key.size = 32;
    owner = {};
    fakeService = {};
    fakeRouter = {};
    fakeNodeDB = {};
    for (unsigned n = 0; n < 3; ++n) {
        fakeNodeDB.nodes[n].num = n + 1;
        fakeNodeDB.nodes[n].public_key.size = 32;
    }
    channels = {};
    for (unsigned n : {0u, 2u}) {
        channels.items[n].has_settings = true;
        channels.items[n].role = n ? meshtastic_Channel_Role_SECONDARY : meshtastic_Channel_Role_PRIMARY;
    }
    fakeMillis = 1000;
    fakeUptimeMs = 1000;
    fakeValidTime = 0;
    RadioLibInterface::instance = nullptr;
}

void test_edit_reference_and_bounds()
{
    eko::BadgeComposer<233> c;
    std::string reference;
    size_t cursor = 0;
    std::mt19937 random(20260925);
    for (unsigned step = 0; step < 50000; ++step) {
        auto op = random() % 4;
        if (op == 0) {
            char ch = ' ' + random() % 95;
            bool expected = reference.size() < c.limit;
            assert(c.insert(ch) == expected);
            if (expected)
                reference.insert(cursor++, 1, ch);
        } else if (op == 1) {
            c.erase();
            if (cursor)
                reference.erase(--cursor, 1);
        } else if (op == 2) {
            cursor = random() % (reference.size() + 1);
            c.cursor = cursor;
        } else {
            c.limit = random() % 234;
        }
        assert(reference == c.text && c.cursor == cursor && c.length == reference.size());
    }
    c.clear();
    c.limit = 233;
    for (int n = 0; n < 233; ++n)
        assert(c.insert('W'));
    assert(!c.insert('x') && c.text[233] == 0);
    c.clear();
    assert(!c.insert('\n') && !c.insert(static_cast<char>(0xc3)) && c.length == 0);
}

void test_editor_actions()
{
    eko::BadgeComposer<233> c;
    c.press(3);
    c.press(3);
    assert(std::string(c.text) == "a");
    c.press(2);
    c.press(2);
    assert(c.mode == eko::EditMode::CANCEL && !c.discard);
    c.press(3);
    assert(c.length == 1 && c.mode == eko::EditMode::GROUPS);
    c.press(4);
    c.press(1);
    c.press(3);
    c.press(0);
    assert(c.cursor == 0);
    c.press(2);
    assert(c.cursor == 1);
    c.action = 3;
    assert(c.press(3) == eko::EditEffect::DESTINATION && c.length == 1);
    c.action = 4;
    assert(c.press(3) == eko::EditEffect::REVIEW && c.length == 1);
    c.action = 5;
    c.press(3);
    c.press(1);
    assert(c.press(3) == eko::EditEffect::DISCARDED && c.length == 0);
}

void test_sender_validation_and_packet()
{
    setup();
    printf("Conservative limits: channel=%zu B, PKI DM=%zu B\n", eko::planMessage({NODENUM_BROADCAST, 0}).limit,
           eko::planMessage({2, 0}).limit);
    assert(eko::planMessage({2, 0}).pki);
    assert(eko::planMessage({NODENUM_BROADCAST, 0}).limit < 233);
    for (auto destination : {eko::Destination{NODENUM_BROADCAST, 0}, eko::Destination{2, 0}}) {
        for (bool aead : {false, true}) {
            channels.aead = aead;
            auto plan = eko::planMessage(destination);
            std::string text(plan.limit, 'x');
            assert(std::string(eko::sendMessage(destination, text.c_str())) == "En cola; sin confirm.");
            assert(fakeService.sent.to == destination.node && fakeService.sent.decoded.dest == destination.node);
            assert(fakeService.sent.pki_encrypted == plan.pki);
            assert(fakeService.sent.decoded.payload.size == text.size());
            assert(fakeService.sent.want_ack == (destination.node != NODENUM_BROADCAST));
            auto data = fakeService.sent.decoded;
            data.has_bitfield = true;
            if (!plan.pki)
                data.xeddsa_signature.size = XEDDSA_SIGNATURE_SIZE;
            size_t encoded;
            assert(pb_get_encoded_size(&encoded, &meshtastic_Data_msg, &data));
            assert(encoded + 16 + (plan.pki || aead ? 12 : 0) <= 255);
            int calls = fakeService.calls;
            text += 'x';
            assert(std::string(eko::sendMessage(destination, text.c_str())) == "Texto largo: acortar");
            assert(fakeService.calls == calls);
        }
    }
    int calls = fakeService.calls;
    assert(eko::planMessage({1, 0}).error);
    assert(eko::planMessage({99, 0}).error);
    assert(eko::planMessage({NODENUM_BROADCAST, 1}).error);
    fakeNodeDB.nodes[1].public_key.size = 0;
    assert(std::string(eko::sendMessage({2, 0}, "hola")) == "DM: faltan claves");
    assert(fakeService.calls == calls);
    fakeRouter.fail = true;
    assert(std::string(eko::sendMessage({NODENUM_BROADCAST, 0}, "hola")) == "Sin memoria");
    fakeRouter.fail = false;
    fakeService.result = 7;
    assert(std::string(eko::sendMessage({NODENUM_BROADCAST, 0}, "hola")) == "Error de envio");
    config.lora.tx_enabled = false;
    assert(std::string(eko::sendMessage({NODENUM_BROADCAST, 0}, "hola")) == "Revisar config LoRa");
}

void test_rx_confirmation_and_lock()
{
    setup();
    BadgeUI ui(0x3c);
    ui.page = BadgeUI::EDIT;
    for (char ch : std::string("Hola desde el badge 123"))
        ui.composer.insert(ch);
    ui.draftDestination = {2, 0};
    ui.draftGuard.capture(ui.draftDestination);
    ui.composer.press(4);
    ui.composer.action = 4;
    ui.press(3);
    assert(ui.page == BadgeUI::REVIEW);
    meshtastic_MeshPacket incoming = {};
    incoming.from = 3;
    incoming.id = 101;
    incoming.to = NODENUM_BROADCAST;
    incoming.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    incoming.decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    incoming.decoded.payload.size = 2;
    memcpy(incoming.decoded.payload.bytes, "RX", 2);
    ui.received(&incoming);
    assert(ui.page == BadgeUI::REVIEW && ui.draftDestination.node == 2);
    assert(std::string(ui.composer.text) == "Hola desde el badge 123");
    ui.press(3);
    assert(ui.page == BadgeUI::EDIT_CONFIRM && !ui.pendingSend);
    ui.received(&incoming);
    ui.press(3);
    assert(ui.pendingSend && fakeService.calls == 0);
    ui.runOnce();
    assert(fakeService.calls == 1 && fakeService.sent.to == 2);
    ui.runOnce();
    assert(fakeService.calls == 1);
    assert(std::string(ui.composer.text) == "Hola desde el badge 123");
    ui.page = BadgeUI::CONFIRM;
    ui.index = 2;
    ui.received(&incoming);
    assert(ui.index == 2);
    ui.press(3);
    ui.runOnce();
    assert(std::string(reinterpret_cast<char *>(fakeService.sent.decoded.payload.bytes), fakeService.sent.decoded.payload.size) ==
           "Sí");
}

void test_buttons_wrap_and_no_repeat_send()
{
    setup();
    fakeMillis = 0xffffff00u;
    BadgeUI ui(0x3c);
    ui.page = BadgeUI::EDIT;
    pinState[EKO_DOWN] = 0;
    ui.runOnce();
    fakeMillis += 34;
    ui.runOnce();
    assert(ui.composer.group == 0);
    fakeMillis += 1;
    ui.runOnce();
    assert(ui.composer.group == 1);
    fakeMillis += 599;
    ui.runOnce();
    assert(ui.composer.group == 1);
    fakeMillis += 1;
    ui.runOnce();
    assert(ui.composer.group == 2);
    fakeMillis += 120;
    ui.runOnce();
    assert(ui.composer.group == 3);
    pinState[EKO_DOWN] = 1;
    ui.runOnce();
    fakeMillis += 35;
    ui.runOnce();
    ui.composer.insert('x');
    ui.page = BadgeUI::EDIT_CONFIRM;
    pinState[EKO_SELECT] = 0;
    ui.runOnce();
    fakeMillis += 35;
    ui.runOnce();
    assert(fakeService.calls == 1);
    for (int i = 0; i < 20; ++i) {
        fakeMillis += 120;
        ui.runOnce();
    }
    assert(fakeService.calls == 1 && ui.page == BadgeUI::SEND_RESULT);
}

void test_changed_destination_and_failed_queue_keep_draft()
{
    setup();
    BadgeUI ui(0x3c);
    ui.composer.insert('x');
    ui.draftDestination = {NODENUM_BROADCAST, 2};
    ui.draftGuard.capture(ui.draftDestination);
    ui.page = BadgeUI::EDIT_CONFIRM;
    channels.items[2].role = meshtastic_Channel_Role_DISABLED;
    ui.press(3);
    ui.runOnce();
    assert(fakeService.calls == 0 && std::string(ui.sendResult) == "Canal no disponible");
    assert(std::string(ui.composer.text) == "x");
    ui.draftDestination = {2, 0};
    ui.draftGuard.capture(ui.draftDestination);
    ui.page = BadgeUI::EDIT_CONFIRM;
    fakeNodeDB.count = 1;
    ui.press(3);
    ui.runOnce();
    assert(fakeService.calls == 0 && std::string(ui.sendResult) == "Nodo no disponible");
    fakeNodeDB.count = 3;
    fakeService.result = 7;
    ui.page = BadgeUI::EDIT_CONFIRM;
    ui.press(3);
    ui.runOnce();
    assert(fakeService.calls == 1 && std::string(ui.sendResult) == "Error de envio");
    assert(std::string(ui.composer.text) == "x" && ui.draftDestination.node == 2);
}

void test_picker_and_display_window()
{
    setup();
    BadgeUI ui(0x3c);
    ui.page = BadgeUI::CHANNEL_PICK;
    ui.candidateChannel = 0;
    ui.press(1);
    assert(ui.candidateChannel == 2);
    ui.press(1);
    assert(ui.candidateChannel == 0);
    ui.page = BadgeUI::NODE_PICK;
    ui.candidateNode = 1;
    ui.moveNode(1);
    assert(ui.candidateNode == 2);
    ui.press(3);
    assert(ui.draftDestination.node == 2);
    ui.press(3);
    assert(ui.page == BadgeUI::EDIT);
    for (int i = 0; i < 180; ++i)
        ui.composer.insert('W');
    for (size_t cursor = 0; cursor <= ui.composer.length; ++cursor) {
        ui.composer.cursor = cursor;
        char out[80];
        ui.editorWindow(out, sizeof(out));
        assert(strchr(out, '|') && ui.oled.getStringWidth(out) <= 128);
    }
    ui.render();
    meshtastic::BluetoothStatus pairing;
    pairing.state = meshtastic::BluetoothStatus::ConnectionState::PAIRING;
    auto text = std::string(ui.composer.text);
    ui.bluetooth(&pairing);
    ui.press(3);
    ui.render();
    assert(std::string(ui.composer.text) == text);
}

// Phase 2 pins filtering, unread semantics and stable selection in the
// production UI. A popup must never reinterpret a held key or replace an
// editor; overflow must never read/reply to the new occupant of a reused slot.
// No GPIO audio backend is linked.
meshtastic_MeshPacket incoming_text(uint32_t id = 100, uint32_t from = 2)
{
    meshtastic_MeshPacket p = {};
    p.id = id;
    p.from = from;
    p.to = NODENUM_BROADCAST;
    p.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    p.decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    p.decoded.payload.size = 5;
    memcpy(p.decoded.payload.bytes, "hello", 5);
    return p;
}

void test_phase2_filters_and_unread()
{
    setup();
    BadgeUI ui(0x3c);
    auto p = incoming_text();
    ui.received(nullptr);
    for (auto port : {meshtastic_PortNum_TELEMETRY_APP, meshtastic_PortNum_ROUTING_APP, meshtastic_PortNum_ALERT_APP,
                      meshtastic_PortNum_DETECTION_SENSOR_APP}) {
        p.decoded.portnum = port;
        ui.received(&p);
    }
    p = incoming_text();
    p.from = 0;
    ui.received(&p);
    p.from = 1;
    ui.received(&p);
    p = incoming_text();
    p.to = 3;
    ui.received(&p);
    p.to = 0;
    ui.received(&p);
    p = incoming_text();
    p.id = 0;
    ui.received(&p);
    p = incoming_text();
    p.which_payload_variant = meshtastic_MeshPacket_encrypted_tag;
    ui.received(&p);
    p = incoming_text();
    p.decoded.payload.size = 0;
    ui.received(&p);
    p = incoming_text();
    p.channel = 255;
    ui.received(&p);
    p = incoming_text();
    fakeNodeDB.nodes[1].bitfield = 1 << 4;
    ui.received(&p);
    assert(ui.inbox.size() == 0 && !ui.notifications.token);
    fakeNodeDB.nodes[1].bitfield = 0;
    ui.received(&p);
    const uint32_t first = ui.notifications.token;
    ui.received(&p);
    assert(ui.inbox.size() == 1 && ui.inbox.unreadCount() == 1);
    ui.runOnce();
    ui.runOnce();
    ui.render();
    assert(ui.notifications.banner && ui.inbox.unreadCount() == 1);
    ui.press(2);
    assert(!ui.notifications.banner && ui.page == BadgeUI::STATUS && ui.inbox.unreadCount() == 1);
    p.id++;
    ui.received(&p);
    p.from = 3;
    ui.received(&p);
    assert(ui.inbox.size() == 3);
    ui.openInbox(first);
    ui.dark = true;
    ui.render();
    assert(ui.inbox.unreadCount() == 3);
    ui.dark = false;
    ui.pairing = true;
    ui.render();
    assert(ui.inbox.unreadCount() == 3);
    ui.pairing = false;
    ui.render();
    assert(ui.inbox.unreadCount() == 2);
    ui.textLine = 2;
    p.id++;
    ui.received(&p);
    assert(ui.selectedMessage == first && ui.textLine == 2);
    for (unsigned id = 500; id < 526; ++id) {
        p.id = id;
        ui.received(&p);
    }
    assert(!ui.inbox.find(first) && ui.inbox.limited);
    unsigned unread = ui.inbox.unreadCount();
    ui.render();
    assert(ui.inbox.unreadCount() == unread);
    ui.press(3);
    ui.press(3);
    assert(ui.page == BadgeUI::MESSAGE_ACTIONS && std::string(ui.replyError) == "Ya no disponible");
    setup();
    BadgeUI quiet(0x3c);
    quiet.dark = true;
    fakeMuted = true;
    p = incoming_text();
    quiet.received(&p);
    quiet.runOnce();
    assert(quiet.dark && quiet.inbox.unreadCount() == 1 && !quiet.notifications.token);
    fakeMuted = false;
    fakeWake = false;
    p.id++;
    quiet.received(&p);
    quiet.runOnce();
    assert(quiet.dark && !quiet.notifications.banner && quiet.notifications.ledOn(fakeMillis));
    quiet.press(2);
    quiet.runOnce();
    assert(quiet.notifications.banner);
}

void test_phase2_draft_reply_and_history()
{
    setup();
    BadgeUI ui(0x3c);
    ui.page = BadgeUI::EDIT;
    ui.composer.insert('x');
    ui.composer.insert('y');
    ui.composer.cursor = 1;
    ui.draftDestination = {3, 0};
    ui.draftGuard.capture(ui.draftDestination);
    ui.composer.mode = eko::EditMode::ACTIONS;
    ui.composer.action = 6;
    auto p = incoming_text();
    p.channel = 2;
    ui.received(&p);
    ui.runOnce();
    assert(!ui.notifications.banner && ui.composer.cursor == 1);
    ui.press(3);
    assert(ui.page == BadgeUI::MESSAGES);
    ui.press(3);
    ui.press(3);
    assert(ui.page == BadgeUI::REPLY_DRAFT && !ui.discardForReply);
    ui.press(3);
    assert(ui.page == BadgeUI::EDIT && std::string(ui.composer.text) == "xy" && ui.composer.cursor == 1);
    assert(ui.draftDestination.node == 3 && ui.composer.action == 6);
    ui.press(3);
    ui.press(3);
    ui.press(3);
    ui.press(1);
    channels.items[2].role = meshtastic_Channel_Role_DISABLED;
    ui.press(3);
    assert(ui.page == BadgeUI::REPLY_DRAFT && ui.composer.length == 2 && ui.draftDestination.node == 3);
    channels.items[2].role = meshtastic_Channel_Role_SECONDARY;
    ui.press(3);
    assert(ui.page == BadgeUI::EDIT && !ui.composer.length && ui.draftDestination.node == NODENUM_BROADCAST &&
           ui.draftDestination.channel == 2);
    p = incoming_text(200, 3);
    p.to = 1;
    ui.received(&p);
    ui.openInbox(ui.notifications.token);
    ui.press(3);
    ui.press(3);
    assert(ui.page == BadgeUI::EDIT && ui.draftDestination.node == 3);
    ui.composer.insert('a');
    ui.page = BadgeUI::EDIT_CONFIRM;
    ui.press(3);
    ui.runOnce();
    assert(ui.inbox.newest()->outgoing && !ui.inbox.newest()->unread && ui.inbox.newest()->peer == 3);
    assert(ui.inbox.next(ui.inbox.newest()->token, true) == ui.inbox.newest(1)->token);
    assert(ui.page == BadgeUI::MESSAGES && !ui.composer.length);
    ui.composer.insert('a');
    unsigned count = ui.inbox.size();
    fakeService.result = 7;
    ui.page = BadgeUI::EDIT_CONFIRM;
    ui.press(3);
    ui.runOnce();
    assert(ui.inbox.size() == count && ui.composer.length == 1);
    for (auto page : {BadgeUI::EDIT, BadgeUI::REVIEW, BadgeUI::EDIT_CONFIRM, BadgeUI::SEND_KIND, BadgeUI::DEST_KIND,
                      BadgeUI::CHANNEL_PICK, BadgeUI::NODE_PICK, BadgeUI::DEST_INFO, BadgeUI::CONFIRM, BadgeUI::SEND_RESULT}) {
        ui.page = page;
        ++p.id;
        ui.received(&p);
        ui.runOnce();
        assert(ui.page == page && !ui.notifications.banner && std::string(ui.composer.text) == "a");
    }
}

void test_phase2_banner_keys_and_rollover()
{
    setup();
    BadgeUI ui(0x3c);
    ui.index = 2;
    pinState[EKO_SELECT] = 0;
    ui.runOnce();
    fakeMillis += 35;
    auto p = incoming_text();
    ui.received(&p);
    ui.runOnce();
    assert(ui.notifications.banner && !ui.notifications.armed && ui.page == BadgeUI::STATUS);
    fakeMillis += 1000;
    ui.runOnce();
    assert(!ui.notifications.armed);
    pinState[EKO_SELECT] = 1;
    ui.runOnce();
    fakeMillis += 35;
    ui.runOnce();
    ui.runOnce();
    assert(ui.notifications.armed);
    ui.press(3);
    assert(ui.page == BadgeUI::MESSAGES);
    ui.press(2);
    assert(ui.page == BadgeUI::STATUS && ui.index == 2);
    ui.pairing = true;
    ++p.id;
    ui.received(&p);
    ui.runOnce();
    assert(!ui.notifications.banner);
    ui.pairing = false;
    ui.runOnce();
    assert(ui.notifications.banner);
    fakeMillis += 6000;
    ui.runOnce();
    assert(!ui.notifications.banner);
    eko::BadgeNotifications n;
    fakeMillis = 0xffffff00u;
    n.received(1);
    n.tick(fakeMillis, true, false);
    assert(n.banner && n.ledOn(fakeMillis));
    for (int i = 0; i < 60; ++i) {
        fakeMillis += 100;
        if (i < 59)
            n.received(i + 2);
        n.tick(fakeMillis, true, true);
    }
    assert(!n.banner);
    fakeMillis += 1200;
    n.tick(fakeMillis, true, true);
    assert(!n.ledOn(fakeMillis));
}

void test_phase2_utf8_and_long_text()
{
    setup();
    BadgeUI ui(0x3c);
    auto p = incoming_text();
    const std::string text = u8"Mensaje: áéíóú ñ ¿cómo estás? 世界 🙂";
    p.decoded.payload.size = text.size();
    memcpy(p.decoded.payload.bytes, text.data(), text.size());
    ui.received(&p);
    assert(std::string(ui.inbox.newest()->text) == text);
    ui.openInbox();
    for (unsigned row = 0; row < text.size(); ++row) {
        char line[80] = {};
        ui.textLine = row;
        ui.messageLine(ui.inbox.newest()->text, line, sizeof(line));
        assert(!sanitizeUtf8(line, sizeof(line)));
        ui.render();
    }
    ++p.id;
    memset(p.decoded.payload.bytes, 'W', sizeof(p.decoded.payload.bytes));
    p.decoded.payload.size = sizeof(p.decoded.payload.bytes);
    ui.received(&p);
    assert(strlen(ui.inbox.newest()->text) == sizeof(p.decoded.payload.bytes));
    ui.openInbox();
    for (unsigned i = 0; i < 240; ++i) {
        ui.press(1);
        ui.render();
    }
    ++p.id;
    p.decoded.payload.bytes[sizeof(p.decoded.payload.bytes) - 1] = 0xc3;
    ui.received(&p);
    assert(ui.inbox.newest()->text[sizeof(p.decoded.payload.bytes) - 1] == '?');
    ui.page = BadgeUI::EDIT;
    ui.composer.clear();
    for (int n = 0; n < 100; ++n)
        ui.composer.insert('W');
    for (unsigned n = 0; n <= 100; ++n) {
        ui.composer.cursor = n;
        char window[80];
        ui.editorWindow(window, sizeof(window));
        assert(strchr(window, '|') && ui.oled.getStringWidth(window) <= 104);
        ui.render();
    }
    eko::BadgeNotifications notification;
    notification.received(5, eko::NotificationEvent::SYSTEM_ALERT);
    notification.tick(fakeMillis, true, true);
    assert(!notification.banner && !notification.ledOn(fakeMillis));
}

void test_phase2_inbox_stress()
{
    setup();
    eko::BadgeInbox inbox;
    std::vector<eko::InboxMessage> reference;
    std::mt19937 random(20260927);
    uint32_t id = 1;
    for (unsigned step = 0; step < 10000; ++step) {
        if (reference.size() && random() % 3 == 0) {
            auto &entry = reference[random() % reference.size()];
            inbox.find(entry.token)->unread = entry.unread = false;
        } else {
            eko::InboxMessage entry;
            entry.id = ++id;
            entry.from = 2 + random() % 3;
            entry.outgoing = random() % 4 == 0;
            entry.direct = random() % 2;
            entry.channel = random() % 3;
            entry.peer = entry.from;
            strcpy(entry.text, "same text");
            entry.token = inbox.add(entry, fakeMillis);
            entry.unread = !entry.outgoing;
            assert(entry.token);
            if (!entry.outgoing)
                assert(inbox.add(entry, fakeMillis) == 0);
            reference.insert(reference.begin(), entry);
            if (reference.size() > eko::BadgeInbox::CAPACITY)
                reference.pop_back();
        }
        unsigned unread = 0;
        assert(reference.size() == inbox.size());
        for (unsigned n = 0; n < reference.size(); ++n) {
            unread += reference[n].unread;
            assert(inbox.newest(n)->token == reference[n].token);
        }
        assert(unread == inbox.unreadCount());
        fakeMillis += 47;
    }
    eko::BadgeInbox cache;
    fakeMillis = 0xffffff00u;
    eko::InboxMessage m;
    m.from = 2;
    m.id = 1;
    cache.add(m, fakeMillis);
    for (unsigned i = 2; i < eko::BadgeInbox::CAPACITY + 3; ++i) {
        m.id = i;
        cache.add(m, fakeMillis);
    }
    assert(cache.duplicate(2, 1));
    fakeMillis += 600000;
    assert(!cache.duplicate(2, 1));
    assert(cache.duplicate(2, 7));
    printf("Phase2 host metadata: inbox=%zu notifications=%zu UI=%zu\n", sizeof(eko::BadgeInbox), sizeof(eko::BadgeNotifications),
           sizeof(BadgeUI));
}

void test_phase2_rx_ui_stress()
{
    setup();
    BadgeUI ui(0x3c);
    std::mt19937 random(20260928);
    ui.composer.insert('d');
    ui.composer.insert('r');
    ui.draftDestination = {3, 2};
    ui.draftGuard.capture(ui.draftDestination);
    for (unsigned step = 0; step < 10000; ++step) {
        const unsigned op = random() % 6;
        if (op < 3) {
            auto packet = incoming_text(1000 + step, 2 + step % 2);
            packet.to = step % 2 ? 1 : NODENUM_BROADCAST;
            packet.channel = step % 2 ? 0 : 2;
            ui.received(&packet);
            ui.received(&packet);
        } else if (op == 3) {
            ui.page = BadgeUI::EDIT;
            ui.notifications.dismiss();
        } else if (op == 4) {
            ui.page = BadgeUI::STATUS;
        } else {
            if (ui.page != BadgeUI::MESSAGES)
                ui.openInbox();
            ui.press(1);
        }
        fakeMillis += 20;
        ui.runOnce();
        assert(ui.inbox.size() <= eko::BadgeInbox::CAPACITY && ui.inbox.unreadCount() <= ui.inbox.size());
        assert(std::string(ui.composer.text) == "dr" && ui.composer.cursor == 2);
        assert(ui.draftDestination.node == 3 && ui.draftDestination.channel == 2);
        assert(fakeService.calls == 0);
    }
}

#include "phase3_cases.h"
#include "phase4_cases.h"
#include "phase5_cases.h"
#include "sound_test_cases.h"
#undef EKO_BADGE_HOST_TEST
#define main badgeLegacyMain
#include "test_main.cpp"
#undef main
#define EKO_BADGE_HOST_TEST
int main()
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    test_phase5_tools_during_audio_and_tx_error();
    puts("PASS Phase5 tools with L4 audio/RX, TX error and 2h simulated idle");
    test_phase5_activity_bound_and_clock();
    puts("PASS Phase5 100000 events, capacity 16, FIFO and monotonic ages");
    test_phase5_metrics_and_role();
    puts("PASS Phase5 official counters, missing clock/radio, recent window and "
         "Role presence");
    test_phase5_events_and_preservation();
    puts("PASS Phase5 event filtering, BLE transitions, TX queued and draft "
         "preservation");
    test_phase5_navigation_refresh_and_stress();
    puts("PASS Phase5 navigation, OLED widths, no polling/NVS, timeout/wake and "
         "saved replies");
    assert(badgeLegacyMain(0, nullptr) == 0);
    puts("PASS Phase1 original composer cases (assert adapter)");
    test_phase4_node_measurement_presence();
    puts("PASS Phase4 RSSI/telemetry presence and MQTT provenance");
    test_phase4_channels_and_empty_nodes();
    puts("PASS Phase4 channel actions, stable selection and empty node database");
    test_phase4_history_and_threads();
    puts("PASS Phase4 100000-event FIFO and conversation reference");
    test_phase4_ble_guard();
    puts("PASS Phase4 BLE channel/key/LoRa mutation before and after confirm");
    test_phase4_utf8_and_quick_persistence();
    puts("PASS Phase4 UTF8 and quick replies persistence/failure/draft isolation");
    test_phase4_navigation_and_stress();
    puts("PASS Phase4 navigation, 20000 mixed events and 1024 nodes");
    test_sound_signature();
    puts("PASS sound signature: exact timing, L4, manual/automatic parity and "
         "rollover");
    test_sound_burst_off_failures();
    puts("PASS sound bursts: no backlog, cooldown, OFF and all backend failure "
         "stages");
    test_sound_active_ui_and_ble();
    puts("PASS active sound: RX, BLE draft isolation and TX progress");
    test_sound_ui_rx();
    puts("PASS sound UI: persistence failures, RX filters, unread bursts and "
         "held keys");
    test_phase3_audio();
    test_phase3_power_and_settings();
    test_phase3_rf_limits();
    puts("PASS Phase3 audio sequencing, bounded bursts, shutdown, settings, wake "
         "and RF limits");
    test_phase2_filters_and_unread();
    puts("PASS Phase2 filters, duplicate identities, unread, overflow, mute and "
         "wake");
    test_phase2_draft_reply_and_history();
    puts("PASS Phase2 protected draft, reply revalidation, channel/DM threads "
         "and TX acceptance");
    test_phase2_banner_keys_and_rollover();
    puts("PASS Phase2 held buttons, context return, PIN priority, burst and "
         "timer rollover");
    test_phase2_utf8_and_long_text();
    puts("PASS Phase2 long text, production UTF8 sanitizer, scroll and unread "
         "cursor viewport");
    test_phase2_rx_ui_stress();
    puts("PASS Phase2 10000 mixed RX/UI events preserve draft and never transmit");
    test_phase2_inbox_stress();
    puts("PASS Phase2 10000-event reference model and bounded dedup expiry");
    test_edit_reference_and_bounds();
    puts("PASS 50000 randomized edits and bounds");
    test_editor_actions();
    puts("PASS actions, cancellation and cursor rollback");
    test_sender_validation_and_packet();
    puts("PASS sender, real nanopb budgets, destination and queue failures");
    test_rx_confirmation_and_lock();
    puts("PASS RX isolation, confirmation, preset regression and unlocked send");
    test_buttons_wrap_and_no_repeat_send();
    puts("PASS debounce, rollover, repeat and one send per RIGHT press");
    test_picker_and_display_window();
    puts("PASS destination pickers, real font cursor window and BLE overlay");
    test_changed_destination_and_failed_queue_keep_draft();
    puts("PASS changed channel/node and failed queue preserve draft");
    printf("Host sizeof composer=%zu UI=%zu (not ESP32 RAM measurement)\n", sizeof(eko::BadgeComposer<233>), sizeof(BadgeUI));
}
#endif
