/*
 * Lab 11 TODO: implement the parser described in cmd_parser.h, test-first.
 * Start from the logic in Lab 4's rx_consume()/handle_command(), and write
 * the tests in tests/test_cmd_parser.c BEFORE each piece of behaviour.
 */
#include "cmd_parser.h"

void cmd_parser_init(struct cmd_parser *p)
{
    p->len = 0;
    p->overflow = false;
}

bool cmd_parser_feed(struct cmd_parser *p, char c, struct cmd *out)
{
    (void)p;
    (void)c;
    (void)out;
    return false;
}
