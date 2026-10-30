#pragma once
#include "mesh/generated/meshtastic/deviceonly.pb.h"
#include "mesh/generated/meshtastic/mesh.pb.h"
#include "variant.h"
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>

inline uint32_t fakeMillis = 0;
inline uint32_t millis()
{
    return fakeMillis;
}
inline int pinState[32] = {};
inline int digitalRead(unsigned pin)
{
    return pinState[pin];
}
inline unsigned fakeAudioWrites = 0;
inline unsigned fakeDuty = 0, fakeHz = 0, fakeAttaches = 0;
inline bool fakeLedcFail = false;
inline void pinMode(unsigned pin, int mode)
{
    if ((pin == EKO_AUDIO_SD || pin == EKO_AUDIO_PWM) && mode != 0)
        ++fakeAudioWrites;
}
inline void digitalWrite(unsigned pin, int value)
{
    pinState[pin] = value;
    ++fakeAudioWrites;
}
inline bool ledcAttach(unsigned pin, unsigned, unsigned bits)
{
    assert(pin == EKO_AUDIO_PWM && bits == 10);
    assert(pinState[EKO_AUDIO_SD] == 1);
    ++fakeAttaches;
    ++fakeAudioWrites;
    return !fakeLedcFail;
}
inline bool ledcWrite(unsigned pin, unsigned duty)
{
    assert(pin == EKO_AUDIO_PWM && duty <= 8);
    fakeDuty = duty;
    ++fakeAudioWrites;
    return !fakeLedcFail;
}
inline unsigned ledcChangeFrequency(unsigned, unsigned hz, unsigned)
{
    ++fakeAudioWrites;
    fakeHz = hz;
    return fakeLedcFail ? 0 : hz;
}
inline bool ledcDetach(unsigned pin)
{
    assert(pin == EKO_AUDIO_PWM && pinState[EKO_AUDIO_SD] == 1);
    ++fakeAudioWrites;
    return true;
}
#define HIGH 1
#define OUTPUT 2
inline std::map<std::string, uint32_t> fakePrefs;
inline std::map<std::string, std::string> fakePrefBytes;
inline bool fakePrefsFail = false;
inline unsigned fakePrefsWrites = 0;
class Preferences
{
    std::string space;

  public:
    bool begin(const char *name, bool)
    {
        space = name;
        return !fakePrefsFail;
    }
    size_t getBytesLength(const char *key) { return fakePrefBytes[space + key].size(); }
    size_t getBytes(const char *key, void *out, size_t size)
    {
        const auto &s = fakePrefBytes[space + key];
        if (s.size() > size)
            return 0;
        memcpy(out, s.data(), s.size());
        return s.size();
    }
    size_t putBytes(const char *key, const void *data, size_t size)
    {
        if (fakePrefsFail)
            return 0;
        ++fakePrefsWrites;
        fakePrefBytes[space + key] = std::string(static_cast<const char *>(data), size);
        return size;
    }
    void end() {}
    uint32_t getUInt(const char *key, uint32_t fallback)
    {
        auto it = fakePrefs.find(space + key);
        return it == fakePrefs.end() ? fallback : it->second;
    }
    bool getBool(const char *key, bool fallback) { return getUInt(key, fallback); }
    size_t putUInt(const char *key, uint32_t value)
    {
        if (fakePrefsFail)
            return 0;
        ++fakePrefsWrites;
        fakePrefs[space + key] = value;
        return 4;
    }
    size_t putBool(const char *key, bool value) { return putUInt(key, value) ? 1 : 0; }
};
inline void setBluetoothEnable(bool) {}
bool sanitizeUtf8(char *, size_t);
#define INPUT_PULLUP 1
#define INPUT 0
#define LOW 0
#define GEOMETRY_128_32 0
#define I2C_ONE 0
#define NEO_GRB 0
#define NEO_KHZ800 0
#define TEXT_ALIGN_LEFT 0
#define LOG_DEBUG(...) ((void)0)
#define LOG_INFO(...) ((void)0)
#define NODENUM_BROADCAST 0xffffffff
#define RX_SRC_LOCAL 0
#define ERRNO_OK 0
#define MESHTASTIC_HEADER_LENGTH 16
#define MAX_LORA_PAYLOAD_LEN 255
#define MESHTASTIC_PKC_OVERHEAD 12
#define MESHTASTIC_AEAD_OVERHEAD 12
#define XEDDSA_SIGNATURE_SIZE 64
#define BITFIELD_OK_TO_MQTT_SHIFT 0
inline meshtastic_LocalConfig config = {};
inline meshtastic_User owner = {};
inline int radioType = 1;
constexpr int RF95_RADIO = 1;
inline int heldLocks = 0;
namespace concurrency
{
class Lock
{
};
class LockGuard
{
  public:
    explicit LockGuard(Lock *) { ++heldLocks; }
    ~LockGuard() { --heldLocks; }
};
class OSThread
{
  public:
    OSThread(const char *, int) {}
    virtual int32_t runOnce() = 0;
};
} // namespace concurrency
template <class T, class Event> class CallbackObserver
{
  public:
    CallbackObserver(T *, int (T::*)(Event)) {}
    template <class U> void observe(U *) {}
};
namespace meshtastic
{
class Status
{
  public:
    int getStatusType() const { return 1; }
};
class BluetoothStatus : public Status
{
  public:
    enum class ConnectionState { PAIRING, CONNECTED, DISCONNECTED };
    ConnectionState state = ConnectionState::DISCONNECTED;
    ConnectionState getConnectionState() const { return state; }
    std::string getPasskey() const { return "123456"; }
    int onNewStatus = 0;
};
} // namespace meshtastic
constexpr int STATUS_TYPE_BLUETOOTH = 1;
inline meshtastic::BluetoothStatus *bluetoothStatus = nullptr;
inline int *textMessageModule = nullptr;
class SinglePortModule
{
  public:
    SinglePortModule(const char *, int) {}
    virtual bool wantPacket(const meshtastic_MeshPacket *) = 0;
};
class NodeDB
{
  public:
    meshtastic_NodeInfoLite nodes[1024] = {};
    meshtastic_DeviceMetrics metrics = {};
    bool telemetryAvailable = false;
    bool copyNodeTelemetry(uint32_t, meshtastic_DeviceMetrics &out) const
    {
        out = metrics;
        return telemetryAvailable;
    }
    unsigned count = 3;
    uint32_t getNodeNum() { return 1; }
    unsigned getNumMeshNodes() { return count; }
    meshtastic_NodeInfoLite *getMeshNodeByIndex(unsigned n)
    {
        assert(n < count);
        return &nodes[n];
    }
    meshtastic_NodeInfoLite *getMeshNode(uint32_t id)
    {
        for (unsigned n = 0; n < count; ++n)
            if (nodes[n].num == id)
                return &nodes[n];
        return nullptr;
    }
    bool copyPublicKey(uint32_t id, meshtastic_NodeInfoLite_public_key_t &key)
    {
        auto n = getMeshNode(id);
        if (!n || n->public_key.size != 32)
            return false;
        key = n->public_key;
        return true;
    }
};
inline NodeDB fakeNodeDB;
inline NodeDB *nodeDB = &fakeNodeDB;
inline bool nodeInfoLiteHasSnr(const meshtastic_NodeInfoLite *n)
{
    return n && (n->bitfield & (1u << 10));
}
inline bool nodeInfoLiteIsIgnored(const meshtastic_NodeInfoLite *n)
{
    return n->bitfield & (1 << 4);
}
inline bool nodeInfoLiteHasIsUnmessagable(const meshtastic_NodeInfoLite *n)
{
    return n->bitfield & (1 << 8);
}
inline bool nodeInfoLiteIsUnmessagable(const meshtastic_NodeInfoLite *n)
{
    return n->bitfield & (1 << 7);
}
class Channels
{
  public:
    meshtastic_Channel items[3] = {};
    bool aead = false;
    uint8_t getNumChannels() { return 3; }
    uint8_t getPrimaryIndex() { return 0; }
    meshtastic_Channel &getByIndex(unsigned n)
    {
        assert(n < 3);
        return items[n];
    }
    const char *getName(unsigned n) { return getByIndex(n).settings.name; }
    bool usesPublicKey(unsigned) { return true; }
    bool isAEADEnabled(unsigned) { return aead; }
};
inline Channels channels;
class Router
{
  public:
    meshtastic_MeshPacket packet = {};
    bool fail = false;
    int allocations = 0;
    meshtastic_MeshPacket *allocForSending()
    {
        ++allocations;
        if (fail)
            return nullptr;
        packet = {};
        packet.id = 42;
        packet.from = 1;
        packet.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
        return &packet;
    }
};
inline Router fakeRouter;
inline Router *router = &fakeRouter;
class MeshService
{
  public:
    meshtastic_MeshPacket sent = {};
    int calls = 0, result = 0;
    int sendToMesh(meshtastic_MeshPacket *p, int, bool cc)
    {
        assert(heldLocks == 0 && cc);
        ++calls;
        sent = *p;
        return result;
    }
};
inline MeshService fakeService;
inline MeshService *service = &fakeService;
extern const uint8_t ArialMT_Plain_10[];
class SSD1306Wire
{
  public:
    std::string rows[2];
    SSD1306Wire(int, int, int, int, int, int) {}
    bool init() { return true; }
    void flipScreenVertically() {}
    void setFont(const uint8_t *) {}
    void setTextAlignment(int) {}
    unsigned frames = 0;
    bool powered = true;
    void displayOn() { powered = true; }
    void displayOff() { powered = false; }
    void clear() {}
    void display() { ++frames; }
    unsigned getStringWidth(const char *s)
    {
        unsigned width = 0;
        for (; *s; ++s) {
            unsigned c = static_cast<uint8_t>(*s);
            if (c >= ArialMT_Plain_10[2] && c - ArialMT_Plain_10[2] < ArialMT_Plain_10[3])
                width += ArialMT_Plain_10[4 + (c - ArialMT_Plain_10[2]) * 4 + 3];
        }
        return width;
    }
    void drawString(int, int y, const char *s)
    {
        assert(getStringWidth(s) <= 128);
        rows[y / 16] = s;
    }
};
class Adafruit_NeoPixel
{
  public:
    Adafruit_NeoPixel(int, int, int) {}
    void begin() {}
    void clear() {}
    unsigned shows = 0;
    void show() { ++shows; }
    void setPixelColor(int, uint32_t) {}
    uint32_t Color(int r, int g, int b) { return (r << 16) | (g << 8) | b; }
};
class FakeWire
{
  public:
    void beginTransmission(int) {}
    int endTransmission() { return 0; }
};
inline FakeWire Wire;

inline bool fakeMuted = false, fakeWake = true;
inline bool isMutedForPacket(const meshtastic_MeshPacket &)
{
    return fakeMuted;
}
inline bool shouldWakeOnReceivedMessage()
{
    return fakeWake;
}

inline uint64_t fakeUptimeMs = 1000;
inline uint32_t fakeValidTime = 0;
constexpr int RTCQualityDevice = 0;
inline uint32_t getValidTime(int)
{
    return fakeValidTime;
}
inline bool nodeInfoLiteHasUser(const meshtastic_NodeInfoLite *n)
{
    return n->bitfield & (1 << 5);
}
class RadioLibInterface
{
  public:
    static inline RadioLibInterface *instance = nullptr;
    uint32_t rxGood = 0, rxBad = 0, txGood = 0;
};
#ifndef APP_VERSION
#define APP_VERSION 2.8.1.host
#define ystr(s) #s
#define xstr(s) ystr(s)
#endif
