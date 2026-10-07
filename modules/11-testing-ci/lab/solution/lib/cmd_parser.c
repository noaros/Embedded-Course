#include "cmd_parser.h"

#include <string.h>

void cmd_parser_init(struct cmd_parser *p)
{
    p->len = 0;
    p->overflow = false;
}

static bool parse_u32(const char *s, uint32_t *out)
{
    if (*s == '\0') {
        return false;
    }
    uint64_t v = 0;
    for (; *s; s++) {
        if (*s < '0' || *s > '9') {
            return false;
        }
        v = v * 10u + (uint64_t)(*s - '0');
        if (v > UINT32_MAX) {
            return false;
        }
    }
    *out = (uint32_t)v;
    return true;
}

static struct cmd parse_line(char *line)
{
    struct cmd c = {CMD_ERROR, 0};

    /* Trim leading and trailing spaces. */
    while (*line == ' ') {
        line++;
    }
    size_t n = strlen(line);
    while (n > 0 && line[n - 1] == ' ') {
        line[--n] = '\0';
    }

    if (strcmp(line, "start") == 0) {
        c.type = CMD_START;
    } else if (strcmp(line, "stop") == 0) {
        c.type = CMD_STOP;
    } else if (strcmp(line, "stats") == 0) {
        c.type = CMD_STATS;
    } else if (strncmp(line, "rate ", 5) == 0) {
        const char *arg = line + 5;
        while (*arg == ' ') {
            arg++;
        }
        uint32_t hz;
        if (parse_u32(arg, &hz) && hz >= 1u && hz <= CMD_RATE_MAX) {
            c.type = CMD_RATE;
            c.arg = hz;
        }
    }
    return c;
}

bool cmd_parser_feed(struct cmd_parser *p, char c, struct cmd *out)
{
    if (c == '\r' || c == '\n') {
        bool had_line = p->len > 0 || p->overflow;
        if (p->overflow) {
            *out = (struct cmd){CMD_ERROR, 0};
        } else if (p->len > 0) {
            p->line[p->len] = '\0';
            *out = parse_line(p->line);
        }
        cmd_parser_init(p);
        return had_line;
    }
    if (p->len < CMD_LINE_MAX - 1u) {
        p->line[p->len++] = c;
    } else {
        p->overflow = true;
    }
    return false;
}
