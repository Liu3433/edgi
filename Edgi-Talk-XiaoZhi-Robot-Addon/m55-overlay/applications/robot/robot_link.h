#ifndef EDGI_ROBOT_LINK_H
#define EDGI_ROBOT_LINK_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Wire contract shared with arduino/EdgiTalkUnoServo, protocol 1. */
#define ROBOT_UART_BAUD 38400
#define ROBOT_PAN_MIN 20
#define ROBOT_PAN_MAX 160
#define ROBOT_TILT_MIN 30
#define ROBOT_TILT_MAX 150
#define ROBOT_CENTER 90
#define ROBOT_REPLY_MS 400u
#define ROBOT_TX_GAP_MS 200u
#define ROBOT_HEARTBEAT_MS 1000u

typedef enum { ROBOT_WAIT_NONE, ROBOT_WAIT_PONG, ROBOT_WAIT_POSE } robot_wait_t;

/* Only the robot worker accesses this state (under its mutex). No heap/RTOS. */
typedef struct {
    int online, need_center, dirty;
    int desired_pan, desired_tilt;
    int sent_pan, sent_tilt;
    int ack_pan, ack_tilt; /* UNO target acknowledgement, NOT measured position */
    robot_wait_t pending;
    uint32_t last_tx, last_rx, last_byte;
    uint32_t tx_count, ack_count, timeout_count, error_count, bad_lines;
    char line[80];
    size_t length;
    int discard, saw_cr;
} robot_link_t;

void robot_link_init(robot_link_t *link, uint32_t now);
int robot_link_pose(robot_link_t *link, int pan, int tilt);
int robot_link_busy(const robot_link_t *link);
void robot_link_feed(robot_link_t *link, unsigned char byte, uint32_t now);
/* Returns one complete command to transmit, or 0. At most one outstanding. */
size_t robot_link_poll(robot_link_t *link, uint32_t now, char *out, size_t capacity);
void robot_link_failed(robot_link_t *link, uint32_t now);

#ifdef __cplusplus
}
#endif
#endif
