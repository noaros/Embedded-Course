#include <string.h>

#include "cmd_parser.h"
#include "unity.h"

static struct cmd_parser p;

void setUp(void) { cmd_parser_init(&p); }
void tearDown(void) {}

/* Feeds a whole string; returns how many commands came out, the last in *last. */
static int feed(const char *s, struct cmd *last)
{
    int n = 0;
    for (; *s; s++) {
        struct cmd c;
        if (cmd_parser_feed(&p, *s, &c)) {
            *last = c;
            n++;
        }
    }
    return n;
}

static void assert_one(const char *input, enum cmd_type type, uint32_t arg)
{
    struct cmd c = {CMD_ERROR, 12345};
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, feed(input, &c), input);
    TEST_ASSERT_EQUAL_INT_MESSAGE(type, c.type, input);
    if (type == CMD_RATE) {
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(arg, c.arg, input);
    }
}

static void test_simple_commands(void)
{
    assert_one("start\n", CMD_START, 0);
    assert_one("stop\n", CMD_STOP, 0);
    assert_one("stats\n", CMD_STATS, 0);
}

static void test_line_endings(void)
{
    assert_one("start\r", CMD_START, 0);
    struct cmd c;
    /* CRLF is one line: the LF after CR ends an empty line, which is ignored. */
    TEST_ASSERT_EQUAL_INT(1, feed("start\r\n", &c));
    TEST_ASSERT_EQUAL_INT(CMD_START, c.type);
}

static void test_nothing_until_end_of_line(void)
{
    struct cmd c;
    TEST_ASSERT_EQUAL_INT(0, feed("start", &c));
    TEST_ASSERT_EQUAL_INT(1, feed("\n", &c));
}

static void test_empty_lines_are_ignored(void)
{
    struct cmd c;
    TEST_ASSERT_EQUAL_INT(0, feed("\n\r\n\n", &c));
}

static void test_surrounding_spaces_are_ignored(void)
{
    assert_one("  stop  \n", CMD_STOP, 0);
    assert_one("rate   500 \n", CMD_RATE, 500);
}

static void test_commands_are_case_sensitive(void)
{
    assert_one("START\n", CMD_ERROR, 0);
}

static void test_unknown_command(void)
{
    assert_one("reboot\n", CMD_ERROR, 0);
    assert_one("starts\n", CMD_ERROR, 0);
}

static void test_rate_limits(void)
{
    assert_one("rate 1\n", CMD_RATE, 1);
    assert_one("rate 2000000\n", CMD_RATE, 2000000);
    assert_one("rate 0\n", CMD_ERROR, 0);
    assert_one("rate 2000001\n", CMD_ERROR, 0);
}

static void test_rate_bad_arguments(void)
{
    assert_one("rate\n", CMD_ERROR, 0);
    assert_one("rate \n", CMD_ERROR, 0);
    assert_one("rate 12x\n", CMD_ERROR, 0);
    assert_one("rate -5\n", CMD_ERROR, 0);
    assert_one("rate 99999999999\n", CMD_ERROR, 0); /* would overflow uint32 */
}

static void test_longest_line_fits(void)
{
    char line[CMD_LINE_MAX + 1];
    memset(line, ' ', CMD_LINE_MAX - 1 - 5);
    strcpy(line + CMD_LINE_MAX - 1 - 5, "start"); /* 31 characters */
    TEST_ASSERT_EQUAL_UINT(CMD_LINE_MAX - 1, strlen(line));
    strcat(line, "\n");
    assert_one(line, CMD_START, 0);
}

static void test_overlong_line_is_an_error_and_parser_recovers(void)
{
    char line[64];
    memset(line, 'x', 40);
    strcpy(line + 40, "\n");
    assert_one(line, CMD_ERROR, 0);
    assert_one("stats\n", CMD_STATS, 0);
}

static void test_several_commands_in_one_burst(void)
{
    struct cmd c;
    TEST_ASSERT_EQUAL_INT(3, feed("start\nrate 10\nstop\n", &c));
    TEST_ASSERT_EQUAL_INT(CMD_STOP, c.type);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_simple_commands);
    RUN_TEST(test_line_endings);
    RUN_TEST(test_nothing_until_end_of_line);
    RUN_TEST(test_empty_lines_are_ignored);
    RUN_TEST(test_surrounding_spaces_are_ignored);
    RUN_TEST(test_commands_are_case_sensitive);
    RUN_TEST(test_unknown_command);
    RUN_TEST(test_rate_limits);
    RUN_TEST(test_rate_bad_arguments);
    RUN_TEST(test_longest_line_fits);
    RUN_TEST(test_overlong_line_is_an_error_and_parser_recovers);
    RUN_TEST(test_several_commands_in_one_burst);
    return UNITY_END();
}
