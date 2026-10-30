#define PHASE5_CHECK(expr)                                                                                                       \
    do {                                                                                                                         \
        if (!(expr)) {                                                                                                           \
            fprintf(stderr, "Phase5 failure line %d: %s\n", __LINE__, #expr);                                                    \
            exit(1);                                                                                                             \
        }                                                                                                                        \
    } while (false)
// Phase 5 tests actual tools rendering and event integration. They guard
// bounded metadata storage, honest unavailable values, no refresh polling/NVS
// writes, preservation of drafts under external configuration, and unchanged
// audio/RF.
void test_phase5_activity_bound_and_clock()
{
    eko::BadgeActivity log;
    PHASE5_CHECK(!log.newest() && log.size() == 0);
    for (unsigned i = 0; i < 100000; ++i) {
        log.add(eko::ActivityKind::RX, uint64_t(i) * 1000, i, i % 8);
        PHASE5_CHECK(log.size() == (i < 16 ? i + 1 : 16));
        for (unsigned j = 0; j < log.size(); ++j) {
            PHASE5_CHECK(log.newest(j)->node == i - j);
            PHASE5_CHECK(log.newest(j)->atMs == uint64_t(i - j) * 1000);
        }
        PHASE5_CHECK(!log.newest(16));
    }
    setup();
    BadgeUI ui(0x3c);
    eko::ActivityEvent e;
    e.atMs = UINT32_MAX - 5ull;
    fakeUptimeMs = UINT32_MAX + 2000ull;
    char age[24];
    ui.activityAge(&e, age, sizeof(age));
    PHASE5_CHECK(!strcmp(age, "2s"));
    fakeUptimeMs = 1;
    ui.activityAge(&e, age, sizeof(age));
    PHASE5_CHECK(!strcmp(age, "--"));
    printf("Phase5 host sizeof event=%zu activity=%zu UI=%zu; ESP heap "
           "free/min/largest unavailable\n",
           sizeof(eko::ActivityEvent), sizeof(eko::BadgeActivity), sizeof(BadgeUI));
}

void test_phase5_metrics_and_role()
{
    setup();
    BadgeUI ui(0x3c);
    char top[80], bottom[80];
    ui.page = BadgeUI::MESH_STATS;
    ui.toolsRender(top, sizeof(top), bottom, sizeof(bottom));
    PHASE5_CHECK(!strcmp(bottom, "2 / lista local"));
    fakeNodeDB.nodes[1].bitfield = 1 << 4;
    ui.toolsRender(top, sizeof(top), bottom, sizeof(bottom));
    PHASE5_CHECK(!strcmp(bottom, "1 / lista local"));
    ui.toolPage = 1;
    fakeValidTime = 0;
    ui.toolsRender(top, sizeof(top), bottom, sizeof(bottom));
    PHASE5_CHECK(strstr(bottom, "--"));
    fakeValidTime = 1800000000;
    fakeNodeDB.nodes[2].last_heard = fakeValidTime - 7199;
    ui.toolsRender(top, sizeof(top), bottom, sizeof(bottom));
    PHASE5_CHECK(!strcmp(bottom, "1 / lista local"));
    for (auto heard : {0u, fakeValidTime + 1, fakeValidTime - 7200}) {
        fakeNodeDB.nodes[2].last_heard = heard;
        ui.toolsRender(top, sizeof(top), bottom, sizeof(bottom));
        PHASE5_CHECK(!strcmp(bottom, "0 / lista local"));
    }
    RadioLibInterface radio;
    RadioLibInterface::instance = &radio;
    radio.rxGood = 34;
    radio.rxBad = 2;
    radio.txGood = 19;
    ui.toolPage = 2;
    ui.toolsRender(top, sizeof(top), bottom, sizeof(bottom));
    PHASE5_CHECK(!strcmp(bottom, "36"));
    ui.toolPage = 4;
    ui.toolsRender(top, sizeof(top), bottom, sizeof(bottom));
    PHASE5_CHECK(!strcmp(bottom, "19"));
    radio.rxGood = UINT32_MAX;
    ui.toolPage = 2;
    ui.toolsRender(top, sizeof(top), bottom, sizeof(bottom));
    PHASE5_CHECK(!strcmp(bottom,
                         "1")); // Same uint32 rollover semantics as official LocalStats.
    RadioLibInterface::instance = nullptr;
    ui.toolsRender(top, sizeof(top), bottom, sizeof(bottom));
    PHASE5_CHECK(!strcmp(bottom, "--"));
    ui.page = BadgeUI::NODE_INFO;
    ui.candidateNode = 3;
    ui.nodeDetail = 8;
    ui.uxRender(top, sizeof(top), bottom, sizeof(bottom));
    PHASE5_CHECK(!strcmp(bottom, "--"));
    fakeNodeDB.nodes[2].bitfield = 1 << 5;
    fakeNodeDB.nodes[2].role = meshtastic_Config_DeviceConfig_Role_ROUTER;
    ui.uxRender(top, sizeof(top), bottom, sizeof(bottom));
    PHASE5_CHECK(!strcmp(bottom, "ROUTER"));
    // 15 is representable by this enum (0..15), but has no named role.
    // 255 must only exercise the unsigned helper; storing it in the enum is UB.
    fakeNodeDB.nodes[2].role = static_cast<meshtastic_Config_DeviceConfig_Role>(15);
    ui.uxRender(top, sizeof(top), bottom, sizeof(bottom));
    PHASE5_CHECK(!strcmp(bottom, "--"));
    PHASE5_CHECK(!strcmp(BadgeUI::roleName(255), "--"));
    fakeNodeDB.nodes[2].bitfield |= 1 << 10;
    fakeNodeDB.nodes[2].snr = 0;
    ui.nodeDetail = 3;
    ui.uxRender(top, sizeof(top), bottom, sizeof(bottom));
    PHASE5_CHECK(!strcmp(bottom, "SNR 0.0 dB"));
    ui.nodeDetail = 8;
    fakeNodeDB.count = 2;
    ui.uxRender(top, sizeof(top), bottom, sizeof(bottom));
    PHASE5_CHECK(!strcmp(bottom, "Nodo no disponible"));
}

void test_phase5_events_and_preservation()
{
    setup();
    BadgeUI ui(0x3c);
    ui.page = BadgeUI::TOOLS;
    ui.composer.assign("borrador");
    ui.composer.cursor = 3;
    ui.draftDestination = {3, 2};
    ui.draftGuard.capture(ui.draftDestination);
    const unsigned writes = fakePrefsWrites;
    auto p = incoming_text(987);
    ui.received(&p);
    ui.received(&p);
    PHASE5_CHECK(ui.activity.size() == 1 && ui.inbox.unreadCount() == 1);
    fakeMuted = true;
    p.id++;
    ui.received(&p);
    PHASE5_CHECK(ui.activity.size() == 1);
    fakeMuted = false;
    meshtastic::BluetoothStatus status;
    status.state = meshtastic::BluetoothStatus::ConnectionState::CONNECTED;
    ui.bluetooth(&status);
    ui.bluetooth(&status);
    PHASE5_CHECK(ui.activity.size() == 2);
    status.state = meshtastic::BluetoothStatus::ConnectionState::DISCONNECTED;
    ui.bluetooth(&status);
    PHASE5_CHECK(ui.activity.size() == 3);
    ui.notifications.dismiss();
    ui.page = BadgeUI::ACTIVITY;
    ui.toolPage = 2;
    fakeNodeDB.count = 1;
    ui.inbox.clear();
    char top[80], bottom[80];
    ui.toolsRender(top, sizeof(top), bottom, sizeof(bottom));
    PHASE5_CHECK(strstr(bottom, "!00000002"));
    strcpy(channels.items[2].settings.name, "Cambio BLE");
    ui.runOnce();
    PHASE5_CHECK(!strcmp(ui.composer.text, "borrador") && ui.composer.cursor == 3);
    PHASE5_CHECK(ui.draftDestination.node == 3 && ui.draftDestination.channel == 2);
    ui.page = BadgeUI::EDIT_CONFIRM;
    ui.press(3);
    ui.runOnce();
    PHASE5_CHECK(fakeService.calls == 0 && fakePrefsWrites == writes);
    PHASE5_CHECK(!strcmp(ui.composer.text, "borrador"));
    setup();
    BadgeUI tx(0x3c);
    tx.quickDestination = {NODENUM_BROADCAST, 0};
    tx.quickGuard.capture(tx.quickDestination);
    tx.page = BadgeUI::CONFIRM;
    tx.index = 0;
    tx.press(3);
    tx.runOnce();
    PHASE5_CHECK(tx.activity.size() == 1 && tx.activity.newest()->kind == eko::ActivityKind::TX_QUEUED);
    PHASE5_CHECK(fakeService.calls == 1);
}

void test_phase5_navigation_refresh_and_stress()
{
    setup();
    const char saved[] = "Mi respuesta";
    fakePrefBytes["eko_quick4q4"] = std::string(saved, sizeof(saved));
    BadgeUI ui(0x3c);
    PHASE5_CHECK(!strcmp(ui.quickReplies[4], saved));
    const unsigned writes = fakePrefsWrites;
    ui.page = BadgeUI::MENU;
    ui.selection = 11;
    ui.press(3);
    PHASE5_CHECK(ui.page == BadgeUI::TOOLS);
    char top[80], bottom[80];
    for (unsigned selected = 0; selected < 4; ++selected) {
        ui.toolSelection = selected;
        ui.press(3);
        const auto page = ui.page;
        for (unsigned n = 0; n < 30; ++n) {
            ui.toolsRender(top, sizeof(top), bottom, sizeof(bottom));
            PHASE5_CHECK(ui.oled.getStringWidth(top) <= 128 && ui.oled.getStringWidth(bottom) <= 128);
            ui.press(1);
        }
        PHASE5_CHECK(ui.page == page);
        ui.press(2);
        PHASE5_CHECK(ui.page == BadgeUI::TOOLS);
    }
    ui.page = BadgeUI::DIAGNOSTICS;
    ui.toolPage = 1;
    ui.toolsRender(top, sizeof(top), bottom, sizeof(bottom));
    PHASE5_CHECK(strstr(bottom, "--"));
    ui.render();
    const uint32_t draw = ui.lastStatusDraw;
    for (unsigned i = 0; i < 100; ++i) {
        fakeMillis += 20;
        ui.runOnce();
    }
    PHASE5_CHECK(ui.lastStatusDraw == draw); // No periodic tools reads or redraws.
    ui.dark = true;
    ui.render();
    ui.press(3);
    PHASE5_CHECK(!ui.dark && ui.page == BadgeUI::DIAGNOSTICS);
    ui.power.released(true);
    for (unsigned i = 0; i < 10000; ++i) {
        fakeMillis += 20;
        fakeUptimeMs += 20;
        ui.activity.add(eko::ActivityKind::RX, fakeUptimeMs, i);
        ui.runOnce();
        PHASE5_CHECK(ui.activity.size() <= 16);
    }
    PHASE5_CHECK(ui.dark && fakePrefsWrites == writes && fakeService.calls == 0);
    PHASE5_CHECK(!strcmp(ui.quickReplies[4], saved));
    PHASE5_CHECK(eko::effectiveTxPower == 10 && !ui.audio.busy());
}

void test_phase5_tools_during_audio_and_tx_error()
{
    setup();
    BadgeUI ui(0x3c);
    ui.page = BadgeUI::DIAGNOSTICS;
    ui.audio.setEnabled(true);
    PHASE5_CHECK(ui.audio.test(fakeMillis));
    PHASE5_CHECK(ui.runOnce() == 5);
    for (unsigned i = 0; i < 80; ++i) {
        fakeMillis += 5;
        if (i % 8 == 0) {
            auto p = incoming_text(50000 + i);
            ui.received(&p);
            ui.press(1);
        }
        ui.runOnce();
    }
    PHASE5_CHECK(!ui.audio.busy() && fakeAttaches == 1 && pinState[EKO_AUDIO_SD] == HIGH);
    PHASE5_CHECK(ui.inbox.unreadCount() == 10 && ui.activity.size() == 10);
    ui.notifications.dismiss();
    ui.pendingSend = true;
    ui.pendingDestination = {NODENUM_BROADCAST, 0};
    ui.pendingGuard.capture(ui.pendingDestination);
    strcpy(ui.pendingText, "fallo controlado");
    channels.items[0].role = meshtastic_Channel_Role_DISABLED;
    ui.runOnce();
    PHASE5_CHECK(ui.activity.newest()->kind == eko::ActivityKind::TX_ERROR && !fakeService.calls);
    const auto prefs = fakePrefsWrites;
    for (unsigned i = 0; i < 7200; ++i) {
        fakeMillis += 1000;
        fakeUptimeMs += 1000;
        ui.runOnce();
    }
    PHASE5_CHECK(ui.dark && fakePrefsWrites == prefs && !fakeService.calls && !ui.audio.busy());
}
