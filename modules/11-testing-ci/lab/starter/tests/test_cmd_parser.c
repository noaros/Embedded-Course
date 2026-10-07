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

/* TODO, test-first, one behaviour at a time (see cmd_parser.h for the grammar):
 *   - CR, LF and CRLF line endings; nothing is reported until a line ends
 *   - empty lines are ignored
 *   - leading/trailing spaces are ignored; commands are case-sensitive
 *   - unknown commands -> CMD_ERROR
 *   - rate: 1 and 2000000 accepted; 0, 2000001, missing, non-numeric,
 *     negative and > UINT32_MAX arguments rejected
 *   - a 31-character line fits; a longer one is CMD_ERROR and the parser
 *     recovers on the next line
 *   - several commands in one burst of bytes
 * Target: >= 90% line coverage of cmd_parser.c. */

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_simple_commands);
    return UNITY_END();
}
