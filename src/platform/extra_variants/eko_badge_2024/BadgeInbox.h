#pragma once
#include "Throttle.h"
#include <cstdint>
#include <cstring>

namespace eko
{
struct InboxMessage {
    uint32_t token = 0, id = 0, from = 0, peer = 0;
    uint8_t channel = 0;
    bool direct = false, outgoing = false, unread = false;
    int32_t rxRssi = 0;
    bool hasRxRssi = false;
    char text[241] = {};
};

class BadgeInbox
{
  public:
    static constexpr unsigned CAPACITY = 20;
    bool limited = false;

    unsigned size() const { return count; }
    unsigned unreadCount() const
    {
        unsigned result = 0;
        for (const auto &entry : entries)
            result += entry.unread;
        return result;
    }
    InboxMessage *find(uint32_t token)
    {
        for (auto &entry : entries)
            if (token && entry.token == token)
                return &entry;
        return nullptr;
    }
    const InboxMessage *newest(unsigned index = 0) const
    {
        return index < count ? &entries[(head + CAPACITY - 1 - index) % CAPACITY] : nullptr;
    }
    bool duplicate(uint32_t from, uint32_t id) const
    {
        for (const auto &entry : entries)
            if (entry.token && !entry.outgoing && entry.from == from && entry.id == id)
                return true;
        for (const auto &key : recent)
            if (key.id && key.from == from && key.id == id && !Throttle::hasElapsed(key.seen, 600000))
                return true;
        return false;
    }
    uint32_t add(InboxMessage message, uint32_t now)
    {
        if (!message.outgoing && (!message.id || duplicate(message.from, message.id)))
            return 0;
        if (!message.outgoing) {
            recent[recentHead] = {message.from, message.id, now};
            recentHead = (recentHead + 1) % 32;
        }
        limited |= count == CAPACITY;
        if (++nextToken == 0)
            ++nextToken;
        message.token = nextToken;
        message.unread = !message.outgoing;
        entries[head] = message;
        head = (head + 1) % CAPACITY;
        if (count < CAPACITY)
            ++count;
        return message.token;
    }
    uint32_t next(uint32_t token, bool sameThread, bool older = true) const
    {
        unsigned current = count;
        for (unsigned i = 0; i < count; ++i)
            if (newest(i)->token == token)
                current = i;
        if (current == count)
            return 0;
        const auto &source = *newest(current);
        for (unsigned step = 1; step <= count; ++step) {
            const auto &candidate = *newest((current + (older ? step : count - step)) % count);
            if (!sameThread || (candidate.direct == source.direct &&
                                (source.direct ? candidate.peer == source.peer : candidate.channel == source.channel)))
                return candidate.token;
        }
        return token;
    }

    static bool sameThread(const InboxMessage &a, const InboxMessage &b)
    {
        return a.direct == b.direct && (a.direct ? a.peer == b.peer : a.channel == b.channel);
    }
    unsigned threads(uint32_t *tokens = nullptr, bool unreadOnly = false) const
    {
        unsigned total = 0;
        for (unsigned i = 0; i < count; ++i) {
            const auto *m = newest(i);
            bool seen = false;
            for (unsigned j = 0; j < i; ++j)
                seen |= sameThread(*m, *newest(j));
            if (!seen && (!unreadOnly || threadUnread(*m))) {
                if (tokens)
                    tokens[total] = m->token;
                ++total;
            }
        }
        return total;
    }
    unsigned threadUnread(const InboxMessage &m) const
    {
        unsigned n = 0;
        for (unsigned i = 0; i < count; ++i)
            n += sameThread(m, *newest(i)) && newest(i)->unread;
        return n;
    }
    void clear()
    {
        // Keep token sequence: a stale selection must never alias a new entry.
        for (auto &entry : entries)
            entry = {};
        count = head = 0;
        limited = true;
    }

  private:
    struct Recent {
        uint32_t from = 0, id = 0, seen = 0;
    } recent[32];
    InboxMessage entries[CAPACITY];
    uint32_t nextToken = 0;
    unsigned count = 0, head = 0, recentHead = 0;
};
} // namespace eko
