/*
 * Line-based command parser for the Lab 4 data logger, extracted so it can be
 * unit-tested on the host. Byte-at-a-time: feed it whatever the UART
 * delivers; it reports a command when a line ends.
 *
 * Grammar (one command per line, CR, LF or CRLF terminated, case-sensitive,
 * leading/trailing spaces ignored):
 *   start | stop | stats | rate <hz>      with 1 <= hz <= 2000000
 */
#ifndef CMD_PARSER_H
#define CMD_PARSER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CMD_LINE_MAX 32u
#define CMD_RATE_MAX 2000000u

enum cmd_type {
    CMD_START,
    CMD_STOP,
    CMD_STATS,
    CMD_RATE,
    CMD_ERROR, /* unknown command, bad argument, or line too long */
};

struct cmd {
    enum cmd_type type;
    uint32_t arg; /* CMD_RATE: the rate in Hz */
};

struct cmd_parser {
    char line[CMD_LINE_MAX];
    size_t len;
    bool overflow;
};

void cmd_parser_init(struct cmd_parser *p);

/* Feeds one byte. Returns true, with *out filled in, when c ends a non-empty line. */
bool cmd_parser_feed(struct cmd_parser *p, char c, struct cmd *out);

#endif
