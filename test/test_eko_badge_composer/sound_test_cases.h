// Tests BadgeSound and real badge handlers. Pins the same bounded L4 signature
// for manual and received messages, OFF/cancel/error shutdown, persistent settings,
// ignored/duplicate traffic, held keys and burst coalescing without sound backlog.
struct ProbeAudio : FakeAudio {
    unsigned duty = 0;
    std::vector<std::pair<unsigned, uint32_t>> events;
    void setDuty(unsigned value)
    {
        duty = value;
        assert(value == 8);
    }
    bool tone(uint32_t value)
    {
        events.push_back({value, fakeMillis});
        return FakeAudio::tone(value);
    }
    bool quiet()
    {
        events.push_back({0, fakeMillis});
        return FakeAudio::quiet();
    }
    void stop()
    {
        events.push_back({9999, fakeMillis});
        FakeAudio::stop();
    }
};
template <class Sound> void advanceSound(Sound &a, unsigned ms)
{
    fakeMillis += ms;
    a.tick(fakeMillis);
}
void test_sound_signature()
{
    for (bool manual : {false, true}) {
        setup();
        fakeMillis = 0xfffffff0;
        ProbeAudio b;
        eko::BadgeSound<ProbeAudio> a(b);
        assert(!a.received(fakeMillis) && !a.busy());
        if (!manual)
            a.setEnabled(true);
        const auto start = fakeMillis;
        assert(manual ? a.test(fakeMillis) : a.received(fakeMillis));
        assert(b.prepares == 0); // Receive context queues only the initial request, never GPIO.
        a.tick(fakeMillis);
        assert(b.prepares == 1 && b.duty == 8);
        advanceSound(a, 99);
        assert(b.tones == 0);
        advanceSound(a, 1);
        assert(b.hz == 1300);
        advanceSound(a, 69);
        assert(b.hz == 1300);
        advanceSound(a, 1);
        assert(b.hz == 0);
        advanceSound(a, 24);
        assert(b.tones == 1);
        advanceSound(a, 1);
        assert(b.hz == 1600);
        advanceSound(a, 70);
        assert(b.hz == 0);
        advanceSound(a, 25);
        assert(b.hz == 2000);
        advanceSound(a, 69);
        assert(a.busy());
        advanceSound(a, 1);
        assert(!a.busy() && !b.active);
        const unsigned hz[] = {1300, 0, 1600, 0, 2000, 9999};
        const unsigned at[] = {100, 170, 195, 265, 290, 360};
        assert(b.events.size() == 6);
        for (unsigned i = 0; i < 6; ++i) {
            assert(b.events[i].first == hz[i]);
            assert(uint32_t(b.events[i].second - start) == at[i]);
        }
        assert(a.isEnabled() == !manual);
    }
}
void test_sound_burst_off_failures()
{
    setup();
    ProbeAudio b;
    eko::BadgeSound<ProbeAudio> a(b);
    a.setEnabled(true);
    assert(a.received(fakeMillis));
    for (int i = 0; i < 10000; ++i)
        assert(!a.received(fakeMillis));
    a.tick(fakeMillis);
    advanceSound(a, 100);
    for (int i = 0; i < 10000; ++i)
        assert(!a.received(fakeMillis));
    advanceSound(a, 70);
    advanceSound(a, 25);
    advanceSound(a, 70);
    advanceSound(a, 25);
    advanceSound(a, 70);
    assert(b.prepares == 1 && b.tones == 3 && !a.busy());
    advanceSound(a, 5000);
    assert(!a.busy() && b.prepares == 1);
    assert(a.received(fakeMillis));
    a.setEnabled(false);
    a.tick(fakeMillis);
    assert(!a.busy() && !b.active && !a.received(fakeMillis));
    advanceSound(a, 2000);
    assert(a.test(fakeMillis));
    a.tick(fakeMillis);
    a.setEnabled(false);
    assert(!a.busy() && !b.active);
    // Fail prepare, first/second/third tone and both silent gaps independently.
    for (unsigned stage = 0; stage < 6; ++stage) {
        setup();
        ProbeAudio driver;
        eko::BadgeSound<ProbeAudio> sound(driver);
        sound.setEnabled(true);
        assert(sound.test(fakeMillis));
        const unsigned intervals[] = {0, 100, 70, 25, 70, 25};
        for (unsigned step = 0; step <= stage; ++step) {
            if (step == stage)
                driver.ok = false;
            advanceSound(sound, intervals[step]);
        }
        assert(sound.hasFailed() && !sound.busy() && !sound.isEnabled() && !driver.active);
        fakeMillis += 3000;
        assert(!sound.test(fakeMillis));
    }
}
void test_sound_ui_rx()
{
    setup();
    BadgeUI ui(0x3c);
    earlyInitVariant();
    assert(pinState[EKO_AUDIO_SD] == HIGH && !ui.audio.isEnabled());
    ui.page = BadgeUI::SOUND_INFO;
    ui.press(3);
    assert(ui.audio.isEnabled() && fakePrefsWrites == 1);
    BadgeUI restored(0x3c);
    assert(restored.audio.isEnabled());
    const auto writes = fakeAudioWrites;
    auto first = incoming_text(10001);
    ui.received(&first);
    assert(fakeAudioWrites == writes);
    ui.runOnce();
    assert(pinState[EKO_AUDIO_SD] == LOW && fakeDuty == 0 && fakeAttaches == 1);
    for (unsigned i = 0; i < 10; ++i) {
        auto packet = incoming_text(10002 + i);
        ui.received(&packet);
    }
    assert(ui.inbox.unreadCount() == 11);
    const unsigned durations[] = {100, 70, 25, 70, 25, 70};
    for (auto ms : durations) {
        fakeMillis += ms;
        ui.runOnce();
    }
    assert(fakeDuty == 0 && fakeHz == 2000 && pinState[EKO_AUDIO_SD] == HIGH && fakeAttaches == 1);
    fakeMillis += 3000;
    ui.runOnce();
    assert(fakeAttaches == 1);
    ui.received(&first);
    ui.runOnce();
    assert(fakeAttaches == 1);
    fakeMuted = true;
    auto muted = incoming_text(11000);
    ui.received(&muted);
    ui.runOnce();
    assert(fakeAttaches == 1);
    fakeMuted = false;
    ui.notifications.dismiss();
    ui.page = BadgeUI::SOUND_INFO;
    ui.soundAction = 0;
    fakePrefsFail = true;
    ui.press(3);
    assert(!ui.audio.isEnabled() && pinState[EKO_AUDIO_SD] == HIGH);
    assert(std::string(ui.settingsResult) == "No guardado");
    ui.press(3);
    assert(!ui.audio.isEnabled()); // Failed ON must not enable.
    fakePrefsFail = false;
    ui.press(3);
    assert(ui.audio.isEnabled());
    ui.press(3);
    assert(!ui.audio.isEnabled());
    BadgeUI off(0x3c);
    assert(!off.audio.isEnabled());
    ui.press(1);
    assert(ui.soundAction == 1);
    ui.press(3);
    ui.runOnce();
    assert(ui.audio.busy());
    ui.press(2);
    assert(!ui.audio.busy() && pinState[EKO_AUDIO_SD] == HIGH);
    setup();
    BadgeUI held(0x3c);
    held.page = BadgeUI::SOUND_INFO;
    held.soundAction = 1;
    pinState[EKO_SELECT] = LOW;
    held.runOnce();
    fakeMillis += 40;
    held.runOnce();
    for (unsigned i = 0; i < 1000; ++i) {
        fakeMillis += 5;
        held.runOnce();
    }
    assert(fakeAttaches == 1 && !held.audio.busy() && !held.audio.isEnabled());
}

void test_sound_active_ui_and_ble()
{
    // Exercise the real backend's failing LEDC attach path, not only the abstract driver.
    setup();
    BadgeUI broken(0x3c);
    earlyInitVariant();
    fakeLedcFail = true;
    assert(broken.audio.test(fakeMillis));
    broken.runOnce();
    assert(broken.audio.hasFailed() && !broken.audio.busy() && pinState[EKO_AUDIO_SD] == HIGH);
    setup();
    BadgeUI ui(0x3c);
    ui.page = BadgeUI::EDIT;
    ui.composer.assign("preservar");
    ui.composer.cursor = 4;
    ui.draftDestination = {3, 0};
    ui.draftGuard.capture(ui.draftDestination);
    ui.audio.setEnabled(true);
    auto first = incoming_text(20000);
    ui.received(&first);
    assert(ui.runOnce() == 5);
    for (unsigned i = 0; i < 80; ++i) {
        fakeMillis += 5;
        if (i % 4 == 0) {
            auto packet = incoming_text(20001 + i);
            ui.received(&packet);
        }
        if (i == 25) {
            meshtastic::BluetoothStatus status;
            status.state = meshtastic::BluetoothStatus::ConnectionState::CONNECTED;
            ui.bluetooth(&status);
            channels.items[0].settings.psk.size = 1;
            channels.items[0].settings.psk.bytes[0] = 9;
        }
        ui.runOnce();
        assert(!strcmp(ui.composer.text, "preservar") && ui.composer.cursor == 4 && ui.draftDestination.node == 3);
        assert(fakeService.calls == 0 && ui.inbox.size() <= 20);
    }
    assert(fakeAttaches == 1 && !ui.audio.busy() && pinState[EKO_AUDIO_SD] == HIGH);
    setup();
    BadgeUI tx(0x3c);
    assert(tx.audio.test(fakeMillis));
    tx.runOnce();
    tx.quickDestination = {NODENUM_BROADCAST, 0};
    tx.quickGuard.capture(tx.quickDestination);
    tx.page = BadgeUI::CONFIRM;
    tx.index = 0;
    tx.press(3);
    tx.runOnce();
    assert(fakeService.calls == 1 && tx.audio.busy());
    tx.audio.cancel();
    assert(pinState[EKO_AUDIO_SD] == HIGH);
}
