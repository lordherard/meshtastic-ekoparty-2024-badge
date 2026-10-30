// BadgeComposer in platform/extra_variants/eko_badge_2024 owns the bounded ASCII draft.
// Editing and leaving submenus must preserve text until explicit discard; capacity is
// counted in transmitted bytes. These tests guard accidental discard, cursor damage
// and writes beyond the terminator. host_main.cpp additionally exercises the actual
// badge UI and sender with simulated hardware in the standalone Windows harness.
#ifndef EKO_BADGE_HOST_TEST
#include "TestUtil.h"
#include "platform/extra_variants/eko_badge_2024/BadgeComposer.h"
#include <unity.h>

void setUp() {}
void tearDown() {}

void test_insert_and_erase_in_middle()
{
    eko::BadgeComposer<233> editor;
    editor.insert('a');
    editor.insert('c');
    editor.cursor = 1;
    editor.insert('b');
    TEST_ASSERT_EQUAL_STRING("abc", editor.text);
    editor.erase();
    TEST_ASSERT_EQUAL_STRING("ac", editor.text);
    TEST_ASSERT_EQUAL(1, editor.cursor);
}

void test_limit_and_cancel_preserve_draft()
{
    eko::BadgeComposer<233> editor;
    editor.limit = 3;
    for (char ch : {'a', 'b', 'c'})
        TEST_ASSERT_TRUE(editor.insert(ch));
    TEST_ASSERT_FALSE(editor.insert('d'));
    editor.press(2);
    editor.press(3);
    TEST_ASSERT_EQUAL_STRING("abc", editor.text);
    editor.press(2);
    editor.press(1);
    TEST_ASSERT_TRUE(editor.press(3) == eko::EditEffect::DISCARDED);
    TEST_ASSERT_EQUAL_STRING("", editor.text);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_insert_and_erase_in_middle);
    RUN_TEST(test_limit_and_cancel_preserve_draft);
    return UNITY_END();
}
#endif
