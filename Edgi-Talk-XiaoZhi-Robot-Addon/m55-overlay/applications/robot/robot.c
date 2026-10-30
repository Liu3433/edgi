#include "robot.h"
#include <rtdevice.h>
#include <stdlib.h>
#include <string.h>

#define DBG_TAG "robot"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

/* CN5 pin 8=P17.1/SCB5 TX, pin 10=P17.0/SCB5 RX. M33 must release SCB5.
 * HW-221 OE is tied to header 3.3 V as in the supplied wiring diagram. */
#define ROBOT_UART_NAME "uart5"
#define MANUAL_HOLD_MS 10000u
#define MOTION_STEP_MS 700u

enum intent { INTENT_NONE, INTENT_POSE, INTENT_CENTER, INTENT_NOD, INTENT_SHAKE,
              INTENT_LISTEN, INTENT_SAD, INTENT_CURIOUS };
struct pose { int pan, tilt; };
static struct rt_mutex lock;
static rt_bool_t ready;
static rt_device_t uart;
static robot_link_t link;
static rt_bool_t automatic = RT_TRUE;
static robot_state_t app_state = ROBOT_IDLE;
static rt_bool_t state_changed;
static enum intent manual_intent, emotion_intent;
static int manual_pan, manual_tilt;
static rt_bool_t manual_hold;
static rt_uint32_t manual_since, step_since;
static struct pose steps[5];
static unsigned step_count, step_index;

static rt_uint32_t now_ms(void)
{
    return (rt_uint32_t)((rt_uint64_t)rt_tick_get() * 1000 / RT_TICK_PER_SECOND);
}

static void clear_motion(void)
{
    step_count = step_index = 0;
    manual_intent = emotion_intent = INTENT_NONE;
    manual_hold = RT_FALSE;
}

static void sequence(enum intent action, rt_uint32_t now)
{
    link.dirty = 0; /* cancel an unsent step from the replaced sequence */
    step_count = step_index = 0;
    steps[step_count++] = (struct pose){90, 90};
    switch (action) {
    case INTENT_POSE: steps[0] = (struct pose){manual_pan, manual_tilt}; break;
    case INTENT_NOD:
        steps[0] = (struct pose){90, 100};
        steps[step_count++] = (struct pose){90, 80};
        steps[step_count++] = (struct pose){90, 100};
        steps[step_count++] = (struct pose){90, 90};
        break;
    case INTENT_SHAKE:
        steps[0] = (struct pose){75, 90};
        steps[step_count++] = (struct pose){105, 90};
        steps[step_count++] = (struct pose){75, 90};
        steps[step_count++] = (struct pose){90, 90};
        break;
    case INTENT_LISTEN: steps[0] = (struct pose){90, 100}; break;
    case INTENT_SAD: steps[0] = (struct pose){90, 80}; break;
    case INTENT_CURIOUS: steps[0] = (struct pose){100, 95}; break;
    default: break;
    }
    step_since = now - MOTION_STEP_MS;
}

static void motion_poll(rt_uint32_t now)
{
    if (!link.online) { clear_motion(); return; }
    if (manual_intent != INTENT_NONE) {
        sequence(manual_intent, now);
        manual_intent = INTENT_NONE;
        manual_hold = RT_TRUE;
        manual_since = now;
        emotion_intent = INTENT_NONE;
    }
    if (manual_hold && (rt_uint32_t)(now - manual_since) >= MANUAL_HOLD_MS) {
        manual_hold = RT_FALSE;
        state_changed = RT_TRUE;
    }
    if (automatic && !manual_hold) {
        if (emotion_intent != INTENT_NONE) {
            sequence(emotion_intent, now);
            emotion_intent = INTENT_NONE;
            state_changed = RT_FALSE;
        } else if (state_changed) {
            sequence(app_state == ROBOT_LISTENING ? INTENT_LISTEN :
                     app_state == ROBOT_SPEAKING ? INTENT_NOD : INTENT_CENTER, now);
            state_changed = RT_FALSE;
        }
    }
    if (step_index < step_count && !robot_link_busy(&link) &&
        (rt_uint32_t)(now - step_since) >= MOTION_STEP_MS) {
        robot_link_pose(&link, steps[step_index].pan, steps[step_index].tilt);
        step_index++;
        step_since = now;
    }
}

rt_err_t robot_set_pose(int pan, int tilt)
{
    rt_err_t result = RT_EOK;
    if (pan < ROBOT_PAN_MIN || pan > ROBOT_PAN_MAX ||
        tilt < ROBOT_TILT_MIN || tilt > ROBOT_TILT_MAX) return -RT_EINVAL;
    if (!ready) return -RT_ERROR;
    rt_mutex_take(&lock, RT_WAITING_FOREVER);
    if (!link.online) result = -RT_EIO;
    else {
        manual_pan = pan;
        manual_tilt = tilt;
        manual_intent = INTENT_POSE; /* latest request wins; no motion backlog */
    }
    rt_mutex_release(&lock);
    return result;
}

rt_err_t robot_action(const char *name)
{
    enum intent action;
    rt_err_t result = RT_EOK;
    if (!name) return -RT_EINVAL;
    if (!strcmp(name, "center")) action = INTENT_CENTER;
    else if (!strcmp(name, "nod")) action = INTENT_NOD;
    else if (!strcmp(name, "shake")) action = INTENT_SHAKE;
    else return -RT_EINVAL;
    if (!ready) return -RT_ERROR;
    rt_mutex_take(&lock, RT_WAITING_FOREVER);
    if (!link.online) result = -RT_EIO;
    else manual_intent = action;
    rt_mutex_release(&lock);
    return result;
}

rt_err_t robot_set_auto(rt_bool_t enabled)
{
    if (!ready) return -RT_ERROR;
    rt_mutex_take(&lock, RT_WAITING_FOREVER);
    automatic = enabled;
    clear_motion();
    link.dirty = 0;
    link.desired_pan = link.pending == ROBOT_WAIT_POSE ? link.sent_pan : link.ack_pan;
    link.desired_tilt = link.pending == ROBOT_WAIT_POSE ? link.sent_tilt : link.ack_tilt;
    state_changed = RT_TRUE;
    /* Stop an automatic sequence at its last target when disabled. */
    rt_mutex_release(&lock);
    return RT_EOK;
}

void robot_on_state(robot_state_t state)
{
    if (!ready) return;
    rt_mutex_take(&lock, RT_WAITING_FOREVER);
    if (app_state != state) {
        app_state = state;
        state_changed = RT_TRUE;
    }
    rt_mutex_release(&lock);
}

void robot_on_emotion(const char *emotion)
{
    enum intent action = INTENT_NONE;
    if (!emotion || !ready) return;
    if (!strcmp(emotion, "happy") || !strcmp(emotion, "laughing") ||
        !strcmp(emotion, "excited")) action = INTENT_NOD;
    else if (!strcmp(emotion, "sad") || !strcmp(emotion, "sleepy")) action = INTENT_SAD;
    else if (!strcmp(emotion, "curious") || !strcmp(emotion, "thinking") ||
             !strcmp(emotion, "confused")) action = INTENT_CURIOUS;
    else if (!strcmp(emotion, "neutral")) action = INTENT_CENTER;
    rt_mutex_take(&lock, RT_WAITING_FOREVER);
    if (automatic && !manual_hold && link.online) emotion_intent = action;
    rt_mutex_release(&lock);
}

rt_err_t robot_get_status(robot_status_t *s)
{
    if (!s) return -RT_EINVAL;
    memset(s, 0, sizeof(*s));
    if (!ready) return -RT_ERROR;
    rt_mutex_take(&lock, RT_WAITING_FOREVER);
    s->online = link.online;
    s->automatic = automatic;
    s->busy = robot_link_busy(&link) || step_index < step_count || manual_intent;
    s->target_pan = link.desired_pan;
    s->target_tilt = link.desired_tilt;
    s->ack_pan = link.ack_pan;
    s->ack_tilt = link.ack_tilt;
    s->tx_count = link.tx_count;
    s->ack_count = link.ack_count;
    s->timeouts = link.timeout_count;
    s->errors = link.error_count;
    s->bad_lines = link.bad_lines;
    rt_mutex_release(&lock);
    return RT_EOK;
}

static void robot_entry(void *parameter)
{
    struct serial_configure config = RT_SERIAL_CONFIG_DEFAULT;
    rt_err_t result;
    int previous_online = 0;
    (void)parameter;
    uart = rt_device_find(ROBOT_UART_NAME);
    if (!uart) { LOG_E("uart5 missing; enable BSP_USING_UART5"); return; }
    config.baud_rate = ROBOT_UART_BAUD;
    config.data_bits = DATA_BITS_8;
    config.stop_bits = STOP_BITS_1;
    config.parity = PARITY_NONE;
    config.bufsz = 256;
    result = rt_device_control(uart, RT_DEVICE_CTRL_CONFIG, &config);
    if (result == RT_EOK)
        result = rt_device_open(uart, RT_DEVICE_OFLAG_RDWR | RT_DEVICE_FLAG_INT_RX);
    if (result != RT_EOK) { LOG_E("uart5 open/config failed: %d", result); return; }
    /* An independent worker owns all RX/TX. No command writes from UI/MCP. */
    LOG_I("uart5 CN5 TX=8/P17.1 RX=10/P17.0, 38400 8N1, probing UNO");
    while (1) {
        unsigned char bytes[64];
        char command[40];
        rt_size_t count, budget = 256;
        rt_uint32_t now = now_ms();
        rt_mutex_take(&lock, RT_WAITING_FOREVER);
        while (budget && (count = rt_device_read(uart, 0, bytes,
                         budget < sizeof(bytes) ? budget : sizeof(bytes))) > 0) {
            rt_size_t i;
            for (i = 0; i < count; i++) robot_link_feed(&link, bytes[i], now);
            budget -= count;
        }
        /* Check expired transactions before processing new intents. */
        if (link.pending != ROBOT_WAIT_NONE &&
            (rt_uint32_t)(now - link.last_tx) >= ROBOT_REPLY_MS) {
            link.timeout_count++;
            robot_link_failed(&link, now);
        }
        motion_poll(now);
        count = robot_link_poll(&link, now, command, sizeof(command));
        if (count && rt_device_write(uart, 0, command, count) != count) {
            link.error_count++;
            robot_link_failed(&link, now);
        }
        if (previous_online != link.online) {
            previous_online = link.online;
            if (link.online) LOG_I("UNO online: PONG and CENTER acknowledged");
            else { clear_motion(); LOG_W("UNO offline; old motion cancelled, probing again"); }
        }
        rt_mutex_release(&lock);
        rt_thread_mdelay(10);
    }
}

static int robot_init(void)
{
    rt_thread_t thread;
    if (rt_mutex_init(&lock, "robot", RT_IPC_FLAG_PRIO) != RT_EOK) return -RT_ERROR;
    robot_link_init(&link, now_ms());
    thread = rt_thread_create("robot", robot_entry, RT_NULL, 3072, 18, 10);
    if (!thread) { rt_mutex_detach(&lock); return -RT_ENOMEM; }
    ready = RT_TRUE;
    if (rt_thread_startup(thread) != RT_EOK) {
        ready = RT_FALSE;
        rt_thread_delete(thread);
        rt_mutex_detach(&lock);
        return -RT_ERROR;
    }
    return RT_EOK;
}
INIT_APP_EXPORT(robot_init);

#ifdef RT_USING_MSH
static int shell_angle(const char *s, int *value)
{
    char *end;
    long n;
    if (!s || *s < '0' || *s > '9' || strlen(s) > 3) return 0;
    n = strtol(s, &end, 10);
    if (*end || n > 180) return 0;
    *value = (int)n;
    return 1;
}

static void robot(int argc, char **argv)
{
    rt_err_t result = -RT_EINVAL;
    int pan, tilt;
    if (argc == 2 && !strcmp(argv[1], "status")) {
        robot_status_t s;
        if (robot_get_status(&s) != RT_EOK) { rt_kprintf("robot not initialized\n"); return; }
        rt_kprintf("uart5 38400 8N1 online=%d auto=%d busy=%d\n", s.online, s.automatic, s.busy);
        rt_kprintf("target=%d,%d UNO_ack=%d,%d (targets, not measured position)\n",
                   s.target_pan, s.target_tilt, s.ack_pan, s.ack_tilt);
        rt_kprintf("tx=%lu ack=%lu timeouts=%lu errors=%lu bad_lines=%lu\n",
                   (unsigned long)s.tx_count, (unsigned long)s.ack_count,
                   (unsigned long)s.timeouts, (unsigned long)s.errors, (unsigned long)s.bad_lines);
        return;
    }
    if (argc == 4 && !strcmp(argv[1], "pose") &&
        shell_angle(argv[2], &pan) && shell_angle(argv[3], &tilt)) result = robot_set_pose(pan, tilt);
    else if (argc == 2) result = robot_action(argv[1]);
    else if (argc == 3 && !strcmp(argv[1], "auto") &&
             (!strcmp(argv[2], "on") || !strcmp(argv[2], "off")))
        result = robot_set_auto(!strcmp(argv[2], "on"));
    if (result == RT_EOK) rt_kprintf("accepted; use robot status to check UNO ACK\n");
    else rt_kprintf("robot error=%d (offline/not ready or invalid argument)\n"
                    "robot status | pose <20..160> <30..150> | center | nod | shake | auto on/off\n", result);
}
MSH_CMD_EXPORT(robot, UNO head control and link status);
#endif
