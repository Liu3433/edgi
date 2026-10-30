#include "robot_link.h"
#include <stdio.h>
#include <string.h>

static int valid_pose(int pan, int tilt)
{
    return pan >= ROBOT_PAN_MIN && pan <= ROBOT_PAN_MAX &&
           tilt >= ROBOT_TILT_MIN && tilt <= ROBOT_TILT_MAX;
}

void robot_link_failed(robot_link_t *s, uint32_t now)
{
    s->online = 0;
    s->pending = ROBOT_WAIT_NONE;
    s->need_center = 0;
    s->dirty = 0;
    s->desired_pan = s->desired_tilt = ROBOT_CENTER;
    s->last_tx = now; /* quiet interval before probing again */
    s->length = 0;
    s->discard = 0;
    s->saw_cr = 0;
}

void robot_link_init(robot_link_t *s, uint32_t now)
{
    memset(s, 0, sizeof(*s));
    robot_link_failed(s, now);
    s->ack_pan = s->ack_tilt = -1;
    s->last_tx = now - ROBOT_HEARTBEAT_MS;
}

int robot_link_pose(robot_link_t *s, int pan, int tilt)
{
    if (!valid_pose(pan, tilt)) return -2;
    if (!s->online) return -1;
    s->desired_pan = pan;
    s->desired_tilt = tilt;
    s->dirty = 1;
    return 0;
}

int robot_link_busy(const robot_link_t *s)
{
    return s->pending != ROBOT_WAIT_NONE || s->dirty;
}

/* Bounded unsigned decimal parser. Reject signs, overflow and suffix garbage. */
static int number(const char **p, int *value)
{
    int n = 0, digits = 0;
    while (**p >= '0' && **p <= '9') {
        if (++digits > 3) return 0;
        n = n * 10 + *(*p)++ - '0';
    }
    *value = n;
    return digits > 0 && n <= 180;
}

static int parse_reply(const char *line, const char *prefix, int *pan, int *tilt)
{
    const char *p;
    size_t n = strlen(prefix);
    if (strncmp(line, prefix, n)) return 0;
    p = line + n;
    if (strncmp(p, ",PAN=", 5)) return 0;
    p += 5;
    if (!number(&p, pan) || strncmp(p, ",TILT=", 6)) return 0;
    p += 6;
    return number(&p, tilt) && *p == '\0' && valid_pose(*pan, *tilt);
}

static void receive_line(robot_link_t *s, uint32_t now)
{
    int pan, tilt;
    if (s->pending != ROBOT_WAIT_NONE &&
        (uint32_t)(now - s->last_tx) >= ROBOT_REPLY_MS) {
        s->timeout_count++;
        robot_link_failed(s, now);
        return; /* a late ACK cannot resurrect an expired transaction */
    }
    if (parse_reply(s->line, "READY,PROTO=1", &pan, &tilt) ||
        parse_reply(s->line, "SAFE,CENTER", &pan, &tilt)) {
        /* Reset or UNO's 5 s watchdog: discard all previous motion intent. */
        robot_link_failed(s, now);
        return;
    }
    if (!strcmp(s->line, "ERR,FORMAT") || !strcmp(s->line, "ERR,RANGE") ||
        !strcmp(s->line, "ERR,OVERFLOW")) {
        s->error_count++;
        /* A probe starts with LF to clear a partial command on UNO. That LF
         * may elicit ERR before the PONG. It is not a successful handshake. */
        if (s->online || s->pending == ROBOT_WAIT_POSE)
            robot_link_failed(s, now);
        return;
    }
    if (parse_reply(s->line, "PONG", &pan, &tilt)) {
        if (s->pending != ROBOT_WAIT_PONG) { s->bad_lines++; return; }
        if (s->online && (pan != s->ack_pan || tilt != s->ack_tilt)) {
            s->error_count++;
            robot_link_failed(s, now);
            return;
        }
        s->pending = ROBOT_WAIT_NONE;
        s->last_rx = now;
        s->ack_count++;
        if (!s->online) s->need_center = 1;
        return;
    }
    if (parse_reply(s->line, "OK", &pan, &tilt)) {
        if (s->pending != ROBOT_WAIT_POSE) { s->bad_lines++; return; }
        if (pan != s->sent_pan || tilt != s->sent_tilt) {
            s->error_count++;
            robot_link_failed(s, now);
            return;
        }
        s->ack_pan = pan;
        s->ack_tilt = tilt;
        s->last_rx = now;
        s->ack_count++;
        s->pending = ROBOT_WAIT_NONE;
        s->online = 1; /* initial CENTER has now also been acknowledged */
        s->need_center = 0;
        return;
    }
    s->bad_lines++;
}

void robot_link_feed(robot_link_t *s, unsigned char byte, uint32_t now)
{
    if ((s->length || s->discard || s->saw_cr) && (uint32_t)(now - s->last_byte) > 150u) {
        s->length = 0;
        s->discard = 1; /* discard fragment through next newline */
    }
    s->last_byte = now;
    if (byte == '\r') { /* allow only one CR immediately before LF */
        if (s->saw_cr) s->discard = 1;
        s->saw_cr = 1;
        return;
    }
    if (byte == '\n') {
        if (s->discard) s->bad_lines++;
        else if (s->length) {
            s->line[s->length] = '\0';
            receive_line(s, now);
        }
        s->length = 0;
        s->discard = 0;
        s->saw_cr = 0;
    } else if (!s->discard) {
        if (s->saw_cr || byte < 32 || byte > 126 || s->length + 1 >= sizeof(s->line))
            s->discard = 1;
        else s->line[s->length++] = (char)byte;
    }
}

size_t robot_link_poll(robot_link_t *s, uint32_t now, char *out, size_t capacity)
{
    int n;
    if (capacity < 32) return 0;
    if (s->pending != ROBOT_WAIT_NONE) {
        if ((uint32_t)(now - s->last_tx) >= ROBOT_REPLY_MS) {
            s->timeout_count++;
            robot_link_failed(s, now);
        }
        return 0;
    }
    /* Leave a turn-around gap for the UNO SoftwareSerial transmitter. */
    if ((uint32_t)(now - s->last_tx) < ROBOT_TX_GAP_MS) return 0;
    if (s->need_center || (s->online && s->dirty)) {
        s->sent_pan = s->need_center ? ROBOT_CENTER : s->desired_pan;
        s->sent_tilt = s->need_center ? ROBOT_CENTER : s->desired_tilt;
        if (s->need_center) n = snprintf(out, capacity, "CENTER\n");
        else n = snprintf(out, capacity, "PAN=%d,TILT=%d\n", s->sent_pan, s->sent_tilt);
        s->pending = ROBOT_WAIT_POSE;
        s->dirty = 0;
    } else if ((uint32_t)(now - s->last_tx) >= ROBOT_HEARTBEAT_MS) {
        n = snprintf(out, capacity, s->online ? "PING\n" : "\nPING\n");
        s->pending = ROBOT_WAIT_PONG;
    } else return 0;
    s->last_tx = now;
    s->tx_count++;
    return (size_t)n;
}
