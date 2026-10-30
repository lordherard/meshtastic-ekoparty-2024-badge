// Read-only tools share BadgeUI's existing mutex and rendering cadence.
eko::BadgeActivity activity;
unsigned toolSelection = 0, toolPage = 0;
bool toolsOverlay = false;

static uint64_t toolsNow()
{
#ifdef EKO_BADGE_HOST_TEST
    return fakeUptimeMs;
#else
    return static_cast<uint64_t>(esp_timer_get_time()) / 1000;
#endif
}

bool toolsPage() const
{
    return page >= TOOLS && page <= ABOUT;
}

bool toolsPress(unsigned key)
{
    if (!toolsPage())
        return false;
    if (key == 2 || key == 4) {
        page = page == TOOLS ? MENU : TOOLS;
        toolPage = 0;
        return true;
    }
    if (page == TOOLS) {
        if (key < 2)
            toolSelection = (toolSelection + (key == 0 ? 3 : 1)) % 4;
        if (key == 3) {
            constexpr Page pages[] = {MESH_STATS, ACTIVITY, DIAGNOSTICS, ABOUT};
            page = pages[toolSelection];
            toolPage = 0;
        }
    } else if (key < 2) {
        const unsigned count = page == MESH_STATS ? 6 : page == DIAGNOSTICS ? 13 : page == ABOUT ? 7 : activity.size();
        if (count)
            toolPage = (toolPage + count + (key == 0 ? -1 : 1)) % count;
    }
    return true;
}

static const char *roleName(unsigned role)
{
    static constexpr const char *names[] = {"CLIENT",      "CLIENT_MUTE", "ROUTER",     "ROUTER_CLIENT", "REPEATER",
                                            "TRACKER",     "SENSOR",      "TAK",        "CLIENT_HIDDEN", "LOST_AND_FOUND",
                                            "TAK_TRACKER", "ROUTER_LATE", "CLIENT_BASE"};
    return role < sizeof(names) / sizeof(names[0]) ? names[role] : "--";
}

void activityAge(const eko::ActivityEvent *event, char *out, size_t size)
{
    if (!event || toolsNow() < event->atMs)
        snprintf(out, size, "--");
    else {
        uint64_t age = (toolsNow() - event->atMs) / 1000;
        const char *unit = "s";
        if (age >= 86400) {
            age /= 86400;
            unit = "d";
        } else if (age >= 3600) {
            age /= 3600;
            unit = "h";
        } else if (age >= 60) {
            age /= 60;
            unit = "m";
        }
        snprintf(out, size, "%llu%s", static_cast<unsigned long long>(age), unit);
    }
}

bool toolsRender(char *top, size_t ts, char *bottom, size_t bs)
{
    if (!toolsPage())
        return false;
    if (page == TOOLS) {
        constexpr const char *names[] = {"Mesh Stats", "Recent Activity", "Diagnostics", "About"};
        snprintf(top, ts, "Mesh Tools %u/4", toolSelection + 1);
        snprintf(bottom, bs, "> %s", names[toolSelection]);
    } else if (page == MESH_STATS) {
        if (toolPage == 0 || toolPage == 1) {
            unsigned registered = 0, recent = 0;
            const uint32_t now = getValidTime(RTCQualityDevice);
            for (unsigned i = 0; i < nodeDB->getNumMeshNodes(); ++i) {
                const auto n = nodeDB->getMeshNodeByIndex(i);
                if (!n || n->num == nodeDB->getNodeNum() || nodeInfoLiteIsIgnored(n))
                    continue;
                ++registered;
                recent += eko::recentlyHeard(n->last_heard, now);
            }
            snprintf(top, ts, "%s", toolPage ? "Recientes (2 h)" : "Nodos registrados");
            if (toolPage && now <= 1600000000u)
                snprintf(bottom, bs, "-- sin reloj");
            else
                snprintf(bottom, bs, "%u / lista local", toolPage ? recent : registered);
        } else if (toolPage >= 2 && toolPage <= 4) {
            const auto radio = RadioLibInterface::instance;
            snprintf(top, ts, "%s",
                     toolPage == 2   ? "RX total / arranque"
                     : toolPage == 3 ? "RX error / arranque"
                                     : "TX radio (sin ACK)");
            if (!radio)
                snprintf(bottom, bs, "--");
            else {
                const uint32_t count = toolPage == 2   ? radio->rxGood + radio->rxBad
                                       : toolPage == 3 ? radio->rxBad
                                                       : radio->txGood;
                snprintf(bottom, bs, "%lu", static_cast<unsigned long>(count));
            }
        } else {
            snprintf(top, ts, "Ult. actividad local");
            activityAge(activity.newest(), bottom, bs);
        }
    } else if (page == ACTIVITY) {
        const auto event = activity.newest(toolPage < activity.size() ? toolPage : 0);
        if (!event) {
            snprintf(top, ts, "Recent Activity");
            snprintf(bottom, bs, "Sin actividad");
        } else {
            constexpr const char *labels[] = {"RX", "TX cola", "TX err", "BLE conectado", "BLE desconectado"};
            if (event->kind == eko::ActivityKind::BLE_UP || event->kind == eko::ActivityKind::BLE_DOWN)
                snprintf(top, ts, "%s", labels[static_cast<unsigned>(event->kind)]);
            else
                snprintf(top, ts, "%s #%u %u/%u", labels[static_cast<unsigned>(event->kind)], event->channel, toolPage + 1,
                         activity.size());
            char age[24];
            activityAge(event, age, sizeof(age));
            if (event->kind == eko::ActivityKind::BLE_UP || event->kind == eko::ActivityKind::BLE_DOWN)
                snprintf(bottom, bs, "%s", age);
            else
                snprintf(bottom, bs, "!%08x %s", event->node, age);
        }
    } else if (page == DIAGNOSTICS) {
        switch (toolPage) {
        case 0:
            snprintf(top, ts, "Uptime");
            snprintf(bottom, bs, "%llu s", static_cast<unsigned long long>(toolsNow() / 1000));
            break;
        case 1:
        case 2:
        case 3:
            snprintf(top, ts, "%s", toolPage == 1 ? "Free heap" : toolPage == 2 ? "Minimum free heap" : "Largest free block");
#ifdef EKO_BADGE_HOST_TEST
            snprintf(bottom, bs, "-- sin hardware");
#else
            snprintf(bottom, bs, "%lu B",
                     static_cast<unsigned long>(toolPage == 1   ? heap_caps_get_free_size(MALLOC_CAP_8BIT)
                                                : toolPage == 2 ? heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT)
                                                                : heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
#endif
            break;
        case 4:
            snprintf(top, ts, "BLE");
            snprintf(bottom, bs, "%s", bleConnected ? "Conectado" : config.bluetooth.enabled ? "Desconectado" : "Apagado");
            break;
        case 5:
            snprintf(top, ts, "Radio %s", radioType == RF95_RADIO ? "inicializada" : "--");
            snprintf(bottom, bs, "TX config: %s", config.lora.tx_enabled ? "ON" : "OFF");
            break;
        case 6:
            if (config.lora.region == meshtastic_Config_LoRaConfig_RegionCode_ANZ)
                snprintf(top, ts, "Region: ANZ");
            else
                snprintf(top, ts, "Region config: %u", static_cast<unsigned>(config.lora.region));
            snprintf(bottom, bs, "Slot config: %lu", static_cast<unsigned long>(config.lora.channel_num));
            break;
        case 7:
            snprintf(top, ts, "Preset config");
            if (config.lora.use_preset && config.lora.modem_preset == meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST)
                snprintf(bottom, bs, "Medium Fast");
            else if (config.lora.use_preset)
                snprintf(bottom, bs, "Preset #%u", static_cast<unsigned>(config.lora.modem_preset));
            else
                snprintf(bottom, bs, "Custom SF%lu", static_cast<unsigned long>(config.lora.spread_factor));
            break;
        case 8: {
            const int effective = eko::effectiveTxPower.load();
            snprintf(top, ts, "TX limite: 10 dBm");
            if (effective >= 2)
                snprintf(bottom, bs, "Efectiva: %d dBm", effective);
            else
                snprintf(bottom, bs, "%s", effective == eko::RF_BLOCKED ? "TX bloqueado" : "Efectiva: --");
            break;
        }
        case 9:
            snprintf(top, ts, "Sound: %s / L4", audio.isEnabled() ? "ON" : "OFF");
            snprintf(bottom, bs, "%s", audio.busy() ? "Audio en curso" : "Audio: shutdown");
            break;
        case 10:
            snprintf(top, ts, "Fuente: --");
            snprintf(bottom, bs, "Bateria local: --");
            break;
        case 11:
            snprintf(top, ts, "Historial RAM %u/20", inbox.size());
            snprintf(bottom, bs, "Actividad RAM %u/16", activity.size());
            break;
        case 12:
            snprintf(top, ts, "Lectura al navegar");
            snprintf(bottom, bs, "Sin sondeos RF");
            break;
        }
    } else if (page == ABOUT) {
        constexpr const char *titles[] = {"MeshBadge",    "Version badge", "Meshtastic", "Build UTC",
                                          "Build fuente", "Hardware",      "MCU"};
        const char *values[] = {"EKOPARTY_RC1", EKO_BADGE_VERSION, xstr(APP_VERSION), EKO_BUILD_UTC,
                                EKO_SOURCE_ID,  "EKOPARTY 2024",   "ESP32-C6"};
        snprintf(top, ts, "%s", titles[toolPage]);
        snprintf(bottom, bs, "%s", values[toolPage]);
    }
    return true;
}
