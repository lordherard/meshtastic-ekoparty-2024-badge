// Tests BadgeAudio/BadgePower/BadgeRfPower and the actual variant UI against
// hardware fakes. Pins nonblocking alerts, no GPIO drive on unverified boards,
// persistent timeouts and safe RF caps. Guards accidental sound activation,
// repeated wake actions, lost drafts, unbounded alert bursts, false saved
// settings and rounding a requested/regional RF limit upwards.
struct FakeAudio {
    bool ok = true, active = false;
    unsigned prepares = 0, stops = 0, tones = 0;
    uint32_t hz = 0;
    bool available() const { return true; }
    bool prepare()
    {
        ++prepares;
        active = true;
        return ok;
    }
    bool tone(uint32_t value)
    {
        ++tones;
        hz = value;
        return ok;
    }
    bool quiet()
    {
        hz = 0;
        return ok;
    }
    void stop()
    {
        ++stops;
        active = false;
        hz = 0;
    }
};

void test_phase3_audio()
{
    setup();
    FakeAudio driver;
    eko::BadgeAudio<FakeAudio> audio(driver);
    audio.received(eko::AudioEvent::DIRECT_MESSAGE);
    audio.tick(fakeMillis);
    assert(driver.prepares == 0);
    audio.setEnabled(true);
    audio.received(eko::AudioEvent::LOW_BATTERY);
    audio.tick(fakeMillis);
    assert(driver.prepares == 0);
    fakeMillis = 0xffffffc0;
    audio.received(eko::AudioEvent::DIRECT_MESSAGE);
    audio.tick(fakeMillis);
    assert(driver.prepares == 1 && driver.tones == 0);
    fakeMillis += 100;
    audio.tick(fakeMillis);
    assert(driver.hz == 1000);
    for (int i = 0; i < 10000; ++i)
        audio.received(eko::AudioEvent::CHANNEL_MESSAGE);
    fakeMillis += 80;
    audio.tick(fakeMillis);
    assert(driver.hz == 0 && audio.busy());
    fakeMillis += 100;
    audio.tick(fakeMillis);
    assert(driver.hz == 1500);
    fakeMillis += 80;
    audio.tick(fakeMillis);
    assert(!driver.active && !audio.busy());
    audio.tick(fakeMillis);
    assert(driver.prepares == 1);
    fakeMillis += 1640;
    audio.tick(fakeMillis);
    assert(driver.prepares == 2);
    audio.setEnabled(false);
    assert(!driver.active);
    audio.setEnabled(true);
    fakeMillis += 3000;
    audio.tick(fakeMillis);
    assert(driver.prepares == 2);
    driver.ok = false;
    audio.received(eko::AudioEvent::CHANNEL_MESSAGE);
    audio.tick(fakeMillis);
    assert(audio.hasFailed() && !audio.isEnabled() && !driver.active);
    BadgeUI ui(0x3c);
    earlyInitVariant();
    assert(pinState[EKO_AUDIO_SD] == HIGH);
    const unsigned writes = fakeAudioWrites;
    auto packet = incoming_text(900);
    ui.received(&packet);
    ui.runOnce();
    assert(!ui.audio.isEnabled() && fakeAudioWrites == writes && fakePrefsWrites == 0);
}

void test_phase3_power_and_settings()
{
    setup();
    BadgeUI ui(0x3c);
    ui.page = BadgeUI::EDIT;
    ui.composer.insert('x');
    ui.runOnce();
    const unsigned frames = ui.oled.frames;
    const unsigned shows = ui.pixels.shows;
    fakeMillis += 200;
    ui.runOnce();
    assert(ui.oled.frames == frames && ui.pixels.shows == shows);
    fakeMillis += 60000;
    ui.runOnce();
    assert(ui.dark && !ui.oled.powered);
    const auto cursor = ui.composer.cursor;
    pinState[EKO_UP] = LOW;
    ui.runOnce();
    fakeMillis += 40;
    ui.runOnce();
    assert(!ui.dark && ui.oled.powered);
    for (int i = 0; i < 15; ++i) {
        fakeMillis += 120;
        ui.runOnce();
    }
    assert(ui.composer.cursor == cursor && std::string(ui.composer.text) == "x");
    pinState[EKO_UP] = HIGH;
    ui.runOnce();
    fakeMillis += 40;
    ui.runOnce();
    ui.runOnce();
    ui.page = BadgeUI::DISPLAY_SETTINGS;
    ui.press(3);
    assert(ui.power.seconds == 0 && fakePrefsWrites == 1);
    BadgeUI reloaded(0x3c);
    assert(reloaded.power.seconds == 0);
    fakePrefsFail = true;
    ui.press(3);
    assert(ui.power.seconds == 0 && std::string(ui.settingsResult) == "No guardado");
    fakePrefsFail = false;
    for (unsigned seconds : {15u, 30u, 60u, 0u}) {
        ui.press(3);
        assert(ui.power.seconds == seconds);
        BadgeUI saved(0x3c);
        assert(saved.power.seconds == seconds);
    }
    fakePrefs["eko_energydisplay_s"] = 123;
    BadgeUI invalid(0x3c);
    assert(invalid.power.seconds == 60);
    eko::BadgePower power;
    for (auto seconds : {15u, 30u, 60u}) {
        power.seconds = seconds;
        fakeMillis = 0xfffffff0;
        power.activity(fakeMillis);
        fakeMillis += seconds * 1000 - 1;
        assert(!power.expired());
        ++fakeMillis;
        assert(power.expired());
    }
    power.seconds = 0;
    assert(!power.expired());
}

void test_phase3_rf_limits()
{
    for (int request = -128; request <= 127; ++request) {
        for (unsigned region : {0u, 1u, 5u, 10u, 14u, 17u, 20u, 30u}) {
            for (int hardware : {10, 14, 17, 20}) {
                const int result = eko::selectTxPower(request, region, hardware);
                if (result == eko::RF_BLOCKED)
                    continue;
                assert(result >= 2 && result <= hardware && result <= 17);
                assert(!region || result <= int(region));
                assert(!request || result <= request);
            }
        }
    }
    assert(eko::selectTxPower(0, 30, 10) == 10);
    assert(eko::selectTxPower(18, 30, 20) == 17);
    assert(eko::selectTxPower(19, 30, 20) == 17);
    assert(eko::selectTxPower(1, 30, 10) == eko::RF_BLOCKED);
    eko::effectiveTxPower = eko::RF_UNAVAILABLE;
    assert(!eko::txPowerReady());
    eko::effectiveTxPower = eko::RF_BLOCKED;
    assert(!eko::txPowerReady());
    eko::effectiveTxPower = 10;
    assert(eko::txPowerReady());
}
