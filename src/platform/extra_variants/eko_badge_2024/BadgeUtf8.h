#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace eko
{
inline size_t utf8Bytes(const char *s)
{
    const auto c = static_cast<uint8_t>(*s);
    size_t n = c < 0x80 ? 1 : c >= 0xc2 && c <= 0xdf ? 2 : c >= 0xe0 && c <= 0xef ? 3 : c >= 0xf0 && c <= 0xf4 ? 4 : 0;
    if (!n || c < 32 || c == 127)
        return 0;
    for (size_t i = 1; i < n; ++i)
        if (!s[i] || (static_cast<uint8_t>(s[i]) & 0xc0) != 0x80)
            return 0;
    if ((c == 0xe0 && static_cast<uint8_t>(s[1]) < 0xa0) || (c == 0xed && static_cast<uint8_t>(s[1]) >= 0xa0) ||
        (c == 0xf0 && static_cast<uint8_t>(s[1]) < 0x90) || (c == 0xf4 && static_cast<uint8_t>(s[1]) >= 0x90))
        return 0;
    return n;
}
inline bool validText(const char *s)
{
    while (*s) {
        size_t n = utf8Bytes(s);
        if (!n)
            return false;
        s += n;
    }
    return true;
}
inline size_t previousChar(const char *s, size_t p)
{
    if (p)
        --p;
    while (p && (static_cast<uint8_t>(s[p]) & 0xc0) == 0x80)
        --p;
    return p;
}
inline size_t nextChar(const char *s, size_t p)
{
    if (!s[p])
        return p;
    ++p;
    while (s[p] && (static_cast<uint8_t>(s[p]) & 0xc0) == 0x80)
        ++p;
    return p;
}
inline unsigned charCount(const char *s)
{
    unsigned n = 0;
    while (*s) {
        auto k = utf8Bytes(s);
        if (!k)
            break;
        s += k;
        ++n;
    }
    return n;
}
inline const char *charAt(const char *s, unsigned p)
{
    while (p-- && *s)
        s += utf8Bytes(s);
    return s;
}
} // namespace eko
