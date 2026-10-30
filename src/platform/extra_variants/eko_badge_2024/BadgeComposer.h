#pragma once
#include "BadgeUtf8.h"
#include <cstddef>
#include <cstdint>

namespace eko
{
enum class EditMode { GROUPS, CHARACTERS, ACTIONS, CURSOR, CANCEL };
enum class EditEffect { NONE, DESTINATION, REVIEW, DISCARDED, INBOX };
inline constexpr const char *GROUPS[] = {"abcde", "fghij",   "klmno",  "pqrst", "uvwxyz", "ABCDE", "FGHIJ",
                                         "KLMNO", "PQRST",   "UVWXYZ", "01234", "56789",  ".,?!:", ";-_/@",
                                         "()+",   "Espacio", "Borrar", "áéíóú", "ñü¿¡"};
inline constexpr const char *ACTIONS[] = {"Continuar",      "Mover cursor", "Borrar",   "Cambiar destino",
                                          "Revisar/enviar", "Cancelar",     "Recibidos"};

template <size_t Capacity> class BadgeComposer
{
  public:
    char text[Capacity + 1] = {};
    size_t length = 0, cursor = 0, limit = Capacity;
    unsigned group = 0, character = 0, action = 0;
    EditMode mode = EditMode::GROUPS;
    bool full = false, discard = false;

    constexpr bool insert(char c)
    {
        if (c < 32 || c > 126 || length >= limit || length >= Capacity) {
            full = true;
            return false;
        }
        for (size_t i = length + 1; i > cursor; --i)
            text[i] = text[i - 1];
        text[cursor++] = c;
        ++length;
        full = false;
        return true;
    }

    bool insertUtf8(const char *s)
    {
        const size_t n = utf8Bytes(s);
        if (!n || length + n > limit || length + n > Capacity) {
            full = true;
            return false;
        }
        memmove(text + cursor + n, text + cursor, length - cursor + 1);
        memcpy(text + cursor, s, n);
        cursor += n;
        length += n;
        full = false;
        return true;
    }
    bool assign(const char *s)
    {
        const size_t n = strlen(s);
        if (n > Capacity || n > limit || !validText(s))
            return false;
        clear();
        memcpy(text, s, n + 1);
        length = cursor = n;
        return true;
    }
    void erase()
    {
        if (cursor) {
            size_t start = previousChar(text, cursor), removed = cursor - start;
            memmove(text + start, text + cursor, length - cursor + 1);
            cursor = start;
            length -= removed;
        }
        full = false;
    }

    constexpr void clear()
    {
        length = cursor = 0;
        text[0] = 0;
        full = false;
        mode = EditMode::GROUPS;
    }

    EditEffect press(unsigned key)
    {
        if (key == 4) {
            if (mode == EditMode::CURSOR)
                cursor = savedCursor;
            mode = EditMode::ACTIONS;
            action = 0;
            return EditEffect::NONE;
        }
        if (mode == EditMode::CANCEL) {
            if (key < 2)
                discard = !discard;
            if (key == 2 || (key == 3 && !discard))
                mode = EditMode::GROUPS;
            else if (key == 3) {
                clear();
                return EditEffect::DISCARDED;
            }
        } else if (mode == EditMode::CURSOR) {
            if (key == 0 && cursor)
                cursor = previousChar(text, cursor);
            if (key == 1 && cursor < length)
                cursor = nextChar(text, cursor);
            if (key == 2)
                cursor = savedCursor;
            if (key == 2 || key == 3)
                mode = EditMode::ACTIONS;
        } else if (mode == EditMode::ACTIONS) {
            if (key < 2)
                action = (action + (key == 0 ? 6 : 1)) % 7;
            if (key == 2)
                mode = EditMode::GROUPS;
            if (key == 3) {
                switch (action) {
                case 0:
                    mode = EditMode::GROUPS;
                    break;
                case 1:
                    savedCursor = cursor;
                    mode = EditMode::CURSOR;
                    break;
                case 2:
                    erase();
                    break;
                case 3:
                    return EditEffect::DESTINATION;
                case 4:
                    return EditEffect::REVIEW;
                case 6:
                    return EditEffect::INBOX;
                case 5:
                    mode = EditMode::CANCEL;
                    discard = false;
                    break;
                }
            }
        } else if (mode == EditMode::CHARACTERS) {
            unsigned n = charCount(GROUPS[group]);
            if (key < 2)
                character = (character + (key == 0 ? n - 1 : 1)) % n;
            if (key == 2)
                mode = EditMode::GROUPS;
            if (key == 3)
                insertUtf8(charAt(GROUPS[group], character));
        } else {
            if (key < 2)
                group = (group + (key == 0 ? 18 : 1)) % 19;
            if (key == 2) {
                mode = EditMode::CANCEL;
                discard = false;
            }
            if (key == 3) {
                if (group == 15)
                    insert(' ');
                else if (group == 16)
                    erase();
                else {
                    mode = EditMode::CHARACTERS;
                    character = 0;
                }
            }
        }
        return EditEffect::NONE;
    }

  private:
    size_t savedCursor = 0;
};
} // namespace eko
