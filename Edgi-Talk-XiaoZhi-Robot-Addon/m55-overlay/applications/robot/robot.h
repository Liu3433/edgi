#ifndef EDGI_ROBOT_H
#define EDGI_ROBOT_H
#include <rtthread.h>
#include "robot_link.h"

#ifdef __cplusplus
extern "C" {
#endif
typedef enum { ROBOT_IDLE, ROBOT_LISTENING, ROBOT_SPEAKING, ROBOT_DISCONNECTED } robot_state_t;
typedef struct {
    int online, automatic, busy;
    int target_pan, target_tilt, ack_pan, ack_tilt;
    rt_uint32_t tx_count, ack_count, timeouts, errors, bad_lines;
} robot_status_t;

#ifdef BSP_USING_ROBOT
/* Nonblocking commands. RT_EOK means queued, not physically reached. */
rt_err_t robot_set_pose(int pan, int tilt);
rt_err_t robot_action(const char *name); /* center / nod / shake */
rt_err_t robot_set_auto(rt_bool_t enabled);
rt_err_t robot_get_status(robot_status_t *status);
void robot_on_state(robot_state_t state);
void robot_on_emotion(const char *emotion);
#else
static inline rt_err_t robot_set_pose(int p, int t) { (void)p; (void)t; return -RT_ERROR; }
static inline rt_err_t robot_action(const char *n) { (void)n; return -RT_ERROR; }
static inline rt_err_t robot_set_auto(rt_bool_t e) { (void)e; return -RT_ERROR; }
static inline rt_err_t robot_get_status(robot_status_t *s) { (void)s; return -RT_ERROR; }
static inline void robot_on_state(robot_state_t s) { (void)s; }
static inline void robot_on_emotion(const char *e) { (void)e; }
#endif
#ifdef __cplusplus
}
#endif
#endif
