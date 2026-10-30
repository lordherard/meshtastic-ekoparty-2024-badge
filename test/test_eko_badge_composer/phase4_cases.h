// Phase 4 exercises actual UI handlers, bounded history and destination snapshots.
// Guards against BLE silently retargeting drafts, UTF-8 cursor corruption,
// history growth and quick-reply persistence overwriting an active editor.
void test_phase4_history_and_threads()
{
    setup();
    eko::BadgeInbox inbox;
    std::vector<eko::InboxMessage> reference;
    for (unsigned i = 1; i <= 100000; ++i) {
        eko::InboxMessage m;
        m.id = i;
        m.from = 2 + i % 7;
        m.peer = m.from;
        m.direct = i % 3 == 0;
        m.channel = i % 3;
        m.outgoing = i % 4 == 0;
        strcpy(m.text, "único texto");
        m.token = inbox.add(m, fakeMillis);
        m.unread = !m.outgoing;
        reference.insert(reference.begin(), m);
        if (reference.size() > 20)
            reference.pop_back();
        assert(inbox.size() == reference.size());
        unsigned threads = 0, unread = 0;
        for (unsigned j = 0; j < reference.size(); ++j) {
            assert(inbox.newest(j)->id == reference[j].id);
            unread += reference[j].unread;
            bool seen = false;
            for (unsigned k = 0; k < j; ++k)
                seen |=
                    reference[j].direct == reference[k].direct &&
                    (reference[j].direct ? reference[j].peer == reference[k].peer : reference[j].channel == reference[k].channel);
            threads += !seen;
        }
        assert(inbox.threads() == threads && inbox.unreadCount() == unread);
    }
    assert(inbox.limited && inbox.newest(19)->id == 99981);
    assert(inbox.next(1, true) == 0);
    auto old = inbox.newest()->token;
    inbox.clear();
    eko::InboxMessage m;
    m.id = 100001;
    m.from = 2;
    assert(inbox.add(m, fakeMillis) != old && !inbox.find(old));
}

void test_phase4_ble_guard()
{
    for (unsigned mutation = 0; mutation < 6; ++mutation) {
        setup();
        BadgeUI ui(0x3c);
        ui.page = BadgeUI::EDIT;
        ui.draftDestination = {NODENUM_BROADCAST, 2};
        ui.draftGuard.capture(ui.draftDestination);
        ui.composer.assign("Texto intacto");
        ui.composer.cursor = 3;
        switch (mutation) {
        case 0:
            strcpy(channels.items[2].settings.name, "Otra red");
            break;
        case 1:
            channels.items[2].settings.psk.size = 1;
            channels.items[2].settings.psk.bytes[0] = 9;
            break;
        case 2:
            channels.items[0].settings.psk.size = 1;
            channels.items[0].settings.psk.bytes[0] = 8;
            break;
        case 3:
            config.lora.channel_num++;
            break;
        case 4:
            owner.is_licensed = true;
            break;
        case 5:
            channels.items[2].role = meshtastic_Channel_Role_DISABLED;
            break;
        }
        ui.page = BadgeUI::EDIT_CONFIRM;
        ui.press(3);
        ui.runOnce();
        assert(!fakeService.calls && !strcmp(ui.composer.text, "Texto intacto") && ui.composer.cursor == 3);
        assert(ui.draftDestination.channel == 2 && ui.draftDestination.node == NODENUM_BROADCAST);
    }
    setup();
    BadgeUI ui(0x3c);
    ui.draftDestination = {2, 0};
    ui.draftGuard.capture(ui.draftDestination);
    ui.composer.assign("DM");
    ui.page = BadgeUI::EDIT_CONFIRM;
    ui.press(3);
    assert(ui.pendingSend);
    // Mutation after confirmation but before dispatch still cannot transmit.
    fakeNodeDB.nodes[1].public_key.bytes[0] = 44;
    ui.runOnce();
    assert(fakeService.calls == 0);
    assert(!strcmp(ui.composer.text, "DM"));
    ui.draftGuard.capture(ui.draftDestination);
    ui.page = BadgeUI::EDIT_CONFIRM;
    ui.press(3);
    ui.runOnce();
    assert(fakeService.calls == 1 && fakeService.sent.to == 2);
}

void test_phase4_utf8_and_quick_persistence()
{
    setup();
    eko::BadgeComposer<32> c;
    assert(c.assign("Sí ñ 🙂"));
    assert(c.cursor == strlen(c.text));
    c.erase();
    assert(!strcmp(c.text, "Sí ñ "));
    c.mode = eko::EditMode::CURSOR;
    c.press(0);
    c.press(0);
    assert(c.cursor == 4);
    assert(c.insertUtf8("¿"));
    assert(eko::validText(c.text));
    assert(!c.insertUtf8("\xc0\xaf") && !c.insertUtf8("\xed\xa0\x80"));
    c.limit = c.length + 1;
    assert(!c.insertUtf8("ñ"));
    BadgeUI ui(0x3c);
    ui.composer.assign("borrador");
    ui.composer.cursor = 2;
    ui.quickSlot = 3;
    ui.smallEditor.assign("En camino ñ");
    assert(ui.saveQuick());
    const auto writes = fakePrefsWrites;
    assert(ui.saveQuick() && fakePrefsWrites == writes);
    BadgeUI reboot(0x3c);
    assert(!strcmp(reboot.quickReplies[3], "En camino ñ"));
    fakePrefsFail = true;
    ui.smallEditor.assign("No guardado");
    assert(!ui.saveQuick());
    assert(!strcmp(ui.quickReplies[3], "En camino ñ") && !strcmp(ui.composer.text, "borrador") && ui.composer.cursor == 2);
    fakePrefsFail = false;
    fakePrefBytes["eko_quick4q3"] = std::string(90, 'x');
    BadgeUI corrupt(0x3c);
    assert(!strcmp(corrupt.quickReplies[3], "No"));
    ui.quickDestination = {2, 0};
    ui.quickGuard.capture(ui.quickDestination);
    ui.quickInThread = true;
    ui.index = 0;
    ui.sendPreset();
    ui.runOnce();
    assert(fakeService.sent.to == 2 && !strcmp(ui.composer.text, "borrador") && ui.composer.cursor == 2);
    assert(ui.page == BadgeUI::MESSAGES);
}

void test_phase4_navigation_and_stress()
{
    setup();
    BadgeUI ui(0x3c);
    ui.page = BadgeUI::EDIT;
    ui.composer.assign("preservar");
    ui.composer.cursor = 4;
    ui.draftDestination = {3, 0};
    ui.draftGuard.capture(ui.draftDestination);
    for (unsigned i = 1; i <= 20000; ++i) {
        auto p = incoming_text(i, 2 + i % 2);
        p.channel = i % 2 ? 0 : 2;
        if (i % 3 == 0)
            p.to = 1;
        ui.received(&p);
        if (i % 50 == 0) {
            meshtastic::BluetoothStatus status;
            status.state = i % 100 ? meshtastic::BluetoothStatus::ConnectionState::CONNECTED
                                   : meshtastic::BluetoothStatus::ConnectionState::DISCONNECTED;
            ui.bluetooth(&status);
        }
        ui.runOnce();
        assert(ui.page == BadgeUI::EDIT && !strcmp(ui.composer.text, "preservar") && ui.composer.cursor == 4 &&
               ui.draftDestination.node == 3);
        assert(ui.inbox.size() <= 20 && fakeService.calls == 0 && fakeAudioWrites == 0 && fakePrefsWrites == 0);
    }
    ui.openThreads();
    assert(ui.page == BadgeUI::THREADS);
    ui.press(3);
    assert(ui.page == BadgeUI::MESSAGES);
    const auto selected = ui.selectedMessage;
    ui.render();
    assert(!ui.inbox.find(selected)->unread);
    ui.press(3);
    ui.press(3);
    assert(ui.page == BadgeUI::REPLY_DRAFT);
    ui.press(3);
    assert(ui.page == BadgeUI::EDIT);
    assert(!strcmp(ui.composer.text, "preservar"));
    ui.composer.clear();
    ui.openInbox(selected);
    ui.press(3);
    ui.press(3);
    assert(ui.page == BadgeUI::EDIT);
    ui.composer.assign("Respuesta");
    ui.page = BadgeUI::EDIT_CONFIRM;
    ui.press(3);
    ui.runOnce();
    assert(ui.page == BadgeUI::MESSAGES && ui.inbox.newest()->outgoing && !ui.composer.length);
    strcpy(channels.items[0].settings.name, "cambio");
    ui.runOnce();
    assert(ui.inbox.size() == 0);
    for (unsigned i = 0; i < 1024; ++i) {
        fakeNodeDB.nodes[i].num = i + 1;
        snprintf(fakeNodeDB.nodes[i].long_name, 25, "Nodo %u", i);
    }
    fakeNodeDB.count = 1024;
    ui.page = BadgeUI::NODES;
    ui.index = 0;
    strcpy(ui.nodeFilter, "Nodo 999");
    ui.moveFilteredNode(1);
    assert(ui.index == 999);
    ui.press(3);
    assert(ui.page == BadgeUI::NODE_INFO && ui.candidateNode == 1000);
    for (unsigned i = 0; i < 8; ++i) {
        ui.nodeDetail = i;
        ui.render();
    }
    assert(fakeAudioWrites == 0 && eko::effectiveTxPower == 10);
}

void test_phase4_channels_and_empty_nodes()
{
    setup();
    BadgeUI ui(0x3c);
    fakeNodeDB.count = 0;
    ui.page = BadgeUI::NODES;
    ui.index = 999;
    ui.render();
    ui.press(3);
    assert(ui.page == BadgeUI::NODES);
    ui.page = BadgeUI::CHANNELS_VIEW;
    ui.candidateChannel = 2;
    ui.channelAction = 0;
    ui.press(3);
    assert(ui.channelAction == 1);
    ui.press(1);
    assert(ui.channelAction == 2);
    ui.press(3);
    assert(ui.page == BadgeUI::DEST_INFO && ui.draftDestination.channel == 2);
    ui.press(3);
    ui.composer.assign("Canal 2");
    ui.page = BadgeUI::EDIT_CONFIRM;
    ui.press(3);
    ui.runOnce();
    assert(fakeService.calls == 1 && fakeService.sent.channel == 2 && ui.page == BadgeUI::MESSAGES);
    const auto m = ui.inbox.newest()->token;
    ui.page = BadgeUI::EDIT;
    auto p = incoming_text(55);
    ui.received(&p);
    assert(ui.inbox.next(m, false, false) == ui.inbox.newest()->token);
    ui.openThreads();
    auto token = ui.threadToken;
    p.id++;
    p.channel = 2;
    ui.received(&p);
    ui.press(3);
    assert(ui.selectedMessage == token);
}

void test_phase4_node_measurement_presence()
{
    setup();
    BadgeUI ui(0x3c);
    ui.page = BadgeUI::NODE_INFO;
    ui.candidateNode = 2;
    ui.nodeDetail = 4;
    ui.render();
    assert(ui.oled.rows[1].find("N/D") != std::string::npos);
    auto p = incoming_text();
    p.has_rx_rssi = true;
    p.rx_rssi = 0;
    ui.received(&p);
    ui.render();
    assert(ui.oled.rows[1].find("0 dBm") != std::string::npos);
    ui.nodeDetail = 5;
    fakeNodeDB.telemetryAvailable = true;
    fakeNodeDB.metrics.has_battery_level = true;
    fakeNodeDB.metrics.battery_level = 0;
    ui.render();
    assert(ui.oled.rows[1].find("0%") != std::string::npos);
    fakeNodeDB.metrics.battery_level = 101;
    ui.render();
    assert(ui.oled.rows[1].find("N/D") != std::string::npos);
    ui.inbox.clear();
    ++p.id;
    p.via_mqtt = true;
    ui.received(&p);
    ui.nodeDetail = 4;
    ui.render();
    assert(ui.oled.rows[1].find("N/D") != std::string::npos);
}
