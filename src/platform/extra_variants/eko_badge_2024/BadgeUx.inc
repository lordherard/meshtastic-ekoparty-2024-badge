// BadgeUI implementation fragment: shares the existing editor, inbox and lock.
eko::DestinationGuard draftGuard, quickGuard, pendingGuard;
eko::Destination quickDestination;
bool returnToConversation = false, quickInThread = false, fromThreads = false;
uint32_t threadToken = 0;
unsigned threadIndex = 0, channelAction = 0, nodeDetail = 0, quickSlot = 0;
bool editingQuick = false;
eko::BadgeComposer<32> smallEditor;
char quickReplies[6][33] = {"OK", "Recibido", "Sí", "No", "En camino", "73"};
char nodeFilter[33] = {};
meshtastic_Channel channelSnapshot[8] = {};
unsigned channelCount = 0;
meshtastic_Config_LoRaConfig historyLora = {};
uint32_t memoryAt = 0, heapEmpty = 0, heapFull = 0;
bool measuredEmpty = false, measuredFull = false;

void loadQuick()
{
    Preferences prefs;
    if (!prefs.begin("eko_quick4", true))
        return;
    for (unsigned n = 0; n < 6; ++n) {
        char key[8], value[33] = {};
        snprintf(key, sizeof(key), "q%u", n);
        const size_t bytes = prefs.getBytesLength(key);
        if (bytes && bytes <= sizeof(value) && prefs.getBytes(key, value, bytes) == bytes && value[bytes - 1] == 0 &&
            strlen(value) + 1 == bytes && eko::validText(value))
            memcpy(quickReplies[n], value, bytes);
    }
    prefs.end();
}

bool saveQuick()
{
    if (!smallEditor.length)
        return false;
    if (!strcmp(quickReplies[quickSlot], smallEditor.text))
        return true;
    Preferences prefs;
    if (!prefs.begin("eko_quick4", false))
        return false;
    char key[8];
    snprintf(key, sizeof(key), "q%u", quickSlot);
    bool ok = prefs.putBytes(key, smallEditor.text, smallEditor.length + 1) == smallEditor.length + 1;
    prefs.end();
    if (ok)
        memcpy(quickReplies[quickSlot], smallEditor.text, smallEditor.length + 1);
    return ok;
}

void rememberChannels()
{
    channelCount = channels.getNumChannels();
    for (unsigned i = 0; i < 8; ++i) {
        if (i < channelCount)
            memcpy(&channelSnapshot[i], &channels.getByIndex(i), sizeof(meshtastic_Channel));
        else
            channelSnapshot[i] = {};
    }
    memcpy(&historyLora, &config.lora, sizeof(historyLora));
}

void checkChannels()
{
    bool changed = channelCount != channels.getNumChannels() || memcmp(&historyLora, &config.lora, sizeof(historyLora));
    for (unsigned i = 0; i < 8 && i < channels.getNumChannels(); ++i)
        changed |= memcmp(&channelSnapshot[i], &channels.getByIndex(i), sizeof(meshtastic_Channel)) != 0;
    if (changed) {
        inbox.clear();
        notifications.dismiss();
        rememberChannels();
        dirty = true;
        // Never refresh draft/quick guards automatically: explicit selection is required.
    }
}

void sampleMemory()
{
#ifndef EKO_BADGE_HOST_TEST
    const bool empty = !measuredEmpty && inbox.size() == 0;
    const bool full = !measuredFull && inbox.size() == eko::BadgeInbox::CAPACITY;
    if (!empty && !full && !Throttle::hasElapsed(memoryAt, 60000))
        return;
    memoryAt = millis();
    const uint32_t free = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    if (empty) {
        heapEmpty = free;
        measuredEmpty = true;
    }
    if (full) {
        heapFull = free;
        measuredFull = true;
    }
    LOG_INFO("EKO_MEM count=%u free=%lu min=%lu largest=%lu stack=%lu ui=%u inbox=%u empty=%lu full=%lu", inbox.size(),
             static_cast<unsigned long>(free), static_cast<unsigned long>(heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT)),
             static_cast<unsigned long>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)),
             static_cast<unsigned long>(uxTaskGetStackHighWaterMark(nullptr)), static_cast<unsigned>(sizeof(BadgeUI)),
             static_cast<unsigned>(sizeof(eko::BadgeInbox)), static_cast<unsigned long>(heapEmpty),
             static_cast<unsigned long>(heapFull));
#endif
}

void openThreads()
{
    inboxReturn = page;
    returnIndex = index;
    returnLine = textLine;
    page = THREADS;
    threadIndex = 0;
    threadToken = inbox.size() ? inbox.newest()->token : 0;
    fromThreads = true;
    notifications.dismiss();
}

void threadLabel(const eko::InboxMessage *m, char *out, size_t size)
{
    if (!m) {
        snprintf(out, size, "Sin mensajes (RAM)");
        return;
    }
    if (m->direct) {
        const auto n = nodeDB->getMeshNode(m->peer);
        if (n && n->short_name[0])
            snprintf(out, size, "@%s [%u]", n->short_name, inbox.threadUnread(*m));
        else
            snprintf(out, size, "@%08x [%u]", m->peer, inbox.threadUnread(*m));
    } else
        snprintf(out, size, "#%u %s [%u]", m->channel,
                 m->channel < channels.getNumChannels() ? channels.getName(m->channel) : "N/D", inbox.threadUnread(*m));
}

meshtastic_NodeInfoLite *listNode()
{
    return index < nodeDB->getNumMeshNodes() ? nodeDB->getMeshNodeByIndex(index) : nullptr;
}

bool nodeMatches(const meshtastic_NodeInfoLite *n)
{
    if (!n || nodeInfoLiteIsIgnored(n))
        return false;
    if (!nodeFilter[0])
        return true;
    char id[10];
    snprintf(id, sizeof(id), "!%08x", n->num);
    return strstr(id, nodeFilter) || strstr(n->short_name, nodeFilter) || strstr(n->long_name, nodeFilter);
}

void moveFilteredNode(int direction)
{
    unsigned count = nodeDB->getNumMeshNodes();
    for (unsigned i = 0; i < count; ++i) {
        index = (index + count + direction) % count;
        if (nodeMatches(listNode()))
            return;
    }
}

bool uxPress(unsigned key)
{
    if (page == THREADS) {
        uint32_t tokens[eko::BadgeInbox::CAPACITY];
        const unsigned total = inbox.threads(tokens);
        if (key == 2 || key == 4) {
            closeInbox();
            return true;
        }
        if (total && key < 2) {
            threadIndex = (threadIndex + total + (key == 0 ? -1 : 1)) % total;
            threadToken = tokens[threadIndex];
        }
        if (total && key == 3 && inbox.find(threadToken)) {
            selectedMessage = threadToken;
            page = MESSAGES;
            textLine = 0;
        }
        return true;
    }
    if (page == MESSAGES && (key == 2 || key == 4) && fromThreads) {
        page = THREADS;
        threadIndex = 0;
        threadToken = inbox.size() ? inbox.newest()->token : 0;
        return true;
    }
    if (page == NODES) {
        if (key < 2)
            moveFilteredNode(key == 0 ? -1 : 1);
        if (key == 2)
            page = MENU;
        if (key == 4) {
            page = NODE_SEARCH;
            smallEditor.clear();
            smallEditor.assign(nodeFilter);
            settingsResult = nullptr;
        }
        if (key == 3) {
            auto n = listNode();
            if (nodeMatches(n)) {
                candidateNode = n->num;
                nodeDetail = 0;
                page = NODE_INFO;
                textLine = 0;
            }
        }
        return true;
    }
    if (page == NODE_INFO) {
        if (key == 2 || key == 4) {
            page = NODES;
            return true;
        }
        if (key < 2) {
            nodeDetail = (nodeDetail + (key == 0 ? 8 : 1)) % 9;
            textLine = 0;
        }
        if (key == 3 && nodeDetail == 7) {
            if (composer.length) {
                editorError = "Borrador conservado";
                page = EDIT;
                return true;
            }
            draftDestination = {candidateNode, channels.getPrimaryIndex()};
            draftGuard.capture(draftDestination);
            returnToConversation = true;
            refreshPlan();
            page = DEST_INFO;
        } else if (key == 3)
            ++textLine;
        return true;
    }
    if (page == CHANNELS_VIEW) {
        if (key == 2 || key == 4) {
            if (channelAction)
                channelAction = 0;
            else
                page = MENU;
            return true;
        }
        if (key < 2) {
            if (channelAction)
                channelAction = channelAction == 1 ? 2 : 1;
            else
                moveChannel(key == 0 ? -1 : 1);
        }
        if (key == 3 && eko::channelEnabled(candidateChannel)) {
            if (channelAction == 0) {
                channelAction = 1;
                return true;
            }
            if (channelAction == 1) {
                // Select toggles into recent messages; empty channels offer composing.
                for (unsigned i = 0; i < inbox.size(); ++i) {
                    auto m = inbox.newest(i);
                    if (!m->direct && m->channel == candidateChannel) {
                        openInbox(m->token);
                        return true;
                    }
                }
                channelAction = 2;
                return true;
            }
            if (composer.length) {
                editorError = "Borrador conservado";
                page = EDIT;
                return true;
            }
            draftDestination = {NODENUM_BROADCAST, static_cast<uint8_t>(candidateChannel)};
            draftGuard.capture(draftDestination);
            returnToConversation = true;
            refreshPlan();
            page = DEST_INFO;
        }
        return true;
    }
    if (page == QUICK_EDIT || page == NODE_SEARCH) {
        if (page == QUICK_EDIT && !editingQuick) {
            if (key < 2)
                quickSlot = (quickSlot + (key == 0 ? 5 : 1)) % 6;
            if (key == 2 || key == 4) {
                page = MENU;
                return true;
            }
            if (key == 3) {
                smallEditor.clear();
                smallEditor.assign(quickReplies[quickSlot]);
                editingQuick = true;
                settingsResult = nullptr;
            }
        } else {
            settingsResult = nullptr;
            auto effect = smallEditor.press(key);
            if (smallEditor.mode == eko::EditMode::ACTIONS && key < 2) {
                while (smallEditor.action == 3 || smallEditor.action == 6)
                    smallEditor.press(key);
            }
            if (effect == eko::EditEffect::DISCARDED) {
                editingQuick = false;
                page = page == NODE_SEARCH ? NODES : QUICK_EDIT;
            }
            if (effect == eko::EditEffect::REVIEW) {
                if (page == NODE_SEARCH) {
                    snprintf(nodeFilter, sizeof(nodeFilter), "%s", smallEditor.text);
                    page = NODES;
                    index = 0;
                    if (!nodeMatches(listNode()))
                        moveFilteredNode(1);
                } else if (saveQuick()) {
                    editingQuick = false;
                    settingsResult = "Guardado";
                } else
                    settingsResult = "No guardado";
            }
        }
        return true;
    }
    return false;
}

bool uxRender(char *top, size_t ts, char *bottom, size_t bs)
{
    if (page == THREADS) {
        uint32_t tokens[eko::BadgeInbox::CAPACITY];
        unsigned total = inbox.threads(tokens);
        snprintf(top, ts, "%s %u/%u", inbox.limited ? "Hist. limitado" : "Mensajes RAM", total ? threadIndex % total + 1 : 0,
                 total);
        auto m = inbox.find(threadToken);
        if (m)
            threadLabel(m, bottom, bs);
        else
            snprintf(bottom, bs, "%s", total ? "Ya no disponible ^v" : "Sin mensajes (RAM)");
    } else if (page == NODES) {
        const auto n = listNode();
        snprintf(top, ts, "Nodos %u / Menu:buscar", nodeDB->getNumMeshNodes());
        if (nodeMatches(n))
            snprintf(bottom, bs, "%s !%08x", n->short_name, n->num);
        else
            snprintf(bottom, bs, "Sin coincidencias");
    } else if (page == NODE_INFO) {
        auto n = nodeDB->getMeshNode(candidateNode);
        snprintf(top, ts, "!%08x %u/9", candidateNode, nodeDetail + 1);
        if (!n) {
            snprintf(bottom, bs, "Nodo no disponible");
            return true;
        }
        switch (nodeDetail) {
        case 0:
            messageLine(n->long_name, bottom, bs);
            break;
        case 1:
            snprintf(bottom, bs, "Short: %s", n->short_name);
            break;
        case 2: {
            uint32_t now = 0;
#ifndef EKO_BADGE_HOST_TEST
            now = getValidTime(RTCQualityDevice);
#endif
            if (n->last_heard > 1600000000 && now >= n->last_heard)
                snprintf(bottom, bs, "Visto hace %lu s", static_cast<unsigned long>(now - n->last_heard));
            else
                snprintf(bottom, bs, "Last heard: N/D");
            break;
        }
        case 3:
            if (nodeInfoLiteHasSnr(n))
                snprintf(bottom, bs, "SNR %.1f dB", n->snr);
            else
                snprintf(bottom, bs, "SNR N/D");
            break;
        case 4: {
            const eko::InboxMessage *sample = nullptr;
            for (unsigned i = 0; i < inbox.size(); ++i) {
                auto m = inbox.newest(i);
                if (!m->outgoing && m->from == candidateNode && m->hasRxRssi) {
                    sample = m;
                    break;
                }
            }
            snprintf(top, ts, "RSSI ultimo salto");
            if (sample)
                snprintf(bottom, bs, "%ld dBm (puede relay)", static_cast<long>(sample->rxRssi));
            else
                snprintf(bottom, bs, "N/D (sin muestra)");
            break;
        }
        case 5: {
            meshtastic_DeviceMetrics metrics = {};
            if (nodeDB->copyNodeTelemetry(candidateNode, metrics) && metrics.has_battery_level && metrics.battery_level <= 100)
                snprintf(bottom, bs, "Bateria remota %lu%%", static_cast<unsigned long>(metrics.battery_level));
            else
                snprintf(bottom, bs, "Bateria N/D");
            break;
        }
        case 6:
            if (n->has_hops_away)
                snprintf(bottom, bs, "Hops %u", n->hops_away);
            else
                snprintf(bottom, bs, "Hops N/D");
            break;
        case 7:
            snprintf(bottom, bs, "> Enviar directo");
            break;
        case 8:
            snprintf(top, ts, "Role registrado");
            snprintf(bottom, bs, "%s", nodeInfoLiteHasUser(n) ? roleName(n->role) : "--");
            break;
        }
    } else if (page == CHANNELS_VIEW) {
        if (!eko::channelEnabled(candidateChannel)) {
            snprintf(top, ts, "Sin canales activos");
            snprintf(bottom, bs, "<: volver");
        } else {
            snprintf(top, ts, "#%u %s%s", candidateChannel, channels.getName(candidateChannel),
                     candidateChannel == channels.getPrimaryIndex() ? " *" : "");
            snprintf(bottom, bs, "%s",
                     channelAction == 0   ? "> Acciones"
                     : channelAction == 1 ? "> Abrir mensajes"
                                          : "> Enviar mensaje");
        }
    } else if (page == QUICK_EDIT || page == NODE_SEARCH) {
        if (page == QUICK_EDIT && !editingQuick) {
            snprintf(top, ts, "Editar rapido %u/6", quickSlot + 1);
            snprintf(bottom, bs, "%s", quickReplies[quickSlot]);
        } else {
            editWindow(smallEditor, top, ts);
            if (settingsResult)
                snprintf(bottom, bs, "%s", settingsResult);
            else if (smallEditor.mode == eko::EditMode::ACTIONS)
                snprintf(bottom, bs, "%s",
                         smallEditor.action == 4 ? (page == NODE_SEARCH ? "> Buscar" : "> Guardar")
                                                 : eko::ACTIONS[smallEditor.action]);
            else if (smallEditor.mode == eko::EditMode::CURSOR)
                snprintf(bottom, bs, "^< v> / >: aceptar");
            else if (smallEditor.mode == eko::EditMode::CANCEL)
                snprintf(bottom, bs, "%s", smallEditor.discard ? "> Cancelar cambios" : "> Seguir");
            else if (smallEditor.mode == eko::EditMode::CHARACTERS)
                snprintf(bottom, bs, "[%.*s] %u/32 B",
                         static_cast<int>(eko::utf8Bytes(eko::charAt(eko::GROUPS[smallEditor.group], smallEditor.character))),
                         eko::charAt(eko::GROUPS[smallEditor.group], smallEditor.character),
                         static_cast<unsigned>(smallEditor.length));
            else
                snprintf(bottom, bs, "%s %u/32 B", eko::GROUPS[smallEditor.group], static_cast<unsigned>(smallEditor.length));
        }
    } else
        return false;
    return true;
}
