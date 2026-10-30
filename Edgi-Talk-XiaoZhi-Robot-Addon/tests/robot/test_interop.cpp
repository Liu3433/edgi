/* Compile the ACTUAL UNO sketch against host clock/serial/servo substitutes.
 * Tests wire protocol and failsafe logic, not electrical/ISR timing. */
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
#include "../../applications/robot/robot_link.h"
#include "../../arduino/EdgiTalkUnoServo/EdgiTalkUnoServo.ino"

static uint32_t clock_ms;
unsigned long millis() { return clock_ms; }
static robot_link_t host;

static void feed(const std::string &s)
{
    for (unsigned char c : s) robot_link_feed(&host, c, ++clock_ms);
}

static void drain(bool deliver = true)
{
    std::string reply = edgiSerial.output;
    edgiSerial.output.clear();
    if (deliver) feed(reply);
}

static void uno_input(const std::string &s)
{
    for (char c : s) {
        ++clock_ms;
        edgiSerial.input.push_back(c);
        loop();
    }
}

static void tick(bool deliver = true)
{
    char command[40];
    size_t n = robot_link_poll(&host, clock_ms, command, sizeof(command));
    if (n) uno_input(std::string(command, n));
    loop();
    drain(deliver);
    clock_ms += 10;
}

static void run_for(uint32_t duration, bool deliver = true)
{
    uint32_t start = clock_ms;
    while ((uint32_t)(clock_ms - start) < duration) tick(deliver);
}

static void reset(uint32_t start = 0)
{
    clock_ms = start;
    rxLength = 0;
    rxOverflow = false;
    panCurrent = tiltCurrent = panTarget = tiltTarget = 90;
    lastServoUpdateMs = start;
    timeoutCenterApplied = false;
    edgiSerial.input.clear();
    edgiSerial.output.clear();
    edgiSerial.overflow_flag = false;
    setup();
    /* READY deliberately missed: M55 may boot later / OE may still be low. */
    edgiSerial.output.clear();
    robot_link_init(&host, clock_ms);
    assert(edgiSerial.baud == ROBOT_UART_BAUD);
}

int main()
{
    reset();
    assert(robot_link_pose(&host, 90, 100) == -1);
    run_for(600);
    assert(host.online && host.ack_pan == 90 && host.ack_tilt == 90);
    assert(host.ack_count >= 2);
    puts("PASS: missed READY, PING/PONG + CENTER handshake");

    assert(robot_link_pose(&host, 100, 120) == 0);
    run_for(1500);
    assert(host.ack_pan == 100 && host.ack_tilt == 120);
    assert(panCurrent == 100 && tiltCurrent == 120);
    assert(robot_link_pose(&host, 19, 90) == -2);
    assert(robot_link_pose(&host, 90, 151) == -2);
    uno_input("PAN=10,TILT=90\n");
    assert(edgiSerial.output.find("ERR,RANGE") != std::string::npos);
    edgiSerial.output.clear();
    assert(panTarget == 100 && tiltTarget == 120);
    puts("PASS: actual UNO parser, CRLF ACK, slew and matching limits");

    uint32_t ack_before = host.ack_count;
    feed("OK,PAN=50,TILT=50\r\nnoise\n");
    assert(host.ack_count == ack_before && host.ack_pan == 100);
    feed(std::string(100, 'X') + "\n");
    feed(std::string("PONG,PAN=90\0,TILT=90\n", 21));
    run_for(1200);
    assert(host.online);
    puts("PASS: unsolicited ACK, garbage, long frames, NUL recovery");

    reset(); run_for(600);
    robot_link_pose(&host, 110, 100);
    run_for(650, false); // the UNO executes, but its ACK is lost
    assert(!host.online && host.timeout_count > 0 && !host.dirty);
    run_for(2000);
    assert(host.online && host.ack_pan == 90 && host.ack_tilt == 90);
    assert(panTarget == 90 && tiltTarget == 90);
    puts("PASS: lost ACK cancels intent; reconnect centers instead of replaying");

    robot_link_pose(&host, 120, 120);
    run_for(800);
    uint32_t no_host_start = clock_ms;
    while ((uint32_t)(clock_ms - no_host_start) < 6500) {
        clock_ms += 20;
        loop();
    }
    assert(panCurrent == 90 && tiltCurrent == 90);
    assert(edgiSerial.output.find("SAFE,CENTER") != std::string::npos);
    drain();
    assert(!host.online);
    run_for(2000);
    assert(host.online);
    puts("PASS: M55 stops sending, UNO watchdog centers after 5 seconds");

    robot_link_pose(&host, 100, 110);
    run_for(800);
    setup(); drain(); // READY during active session invalidates it
    assert(!host.online);
    run_for(2000);
    assert(host.online && host.ack_pan == 90 && host.ack_tilt == 90);
    puts("PASS: UNO resets while M55 remains powered");

    reset();
    uno_input("PAN=120,TILT="); // incomplete command left on UNO
    run_for(1000);
    assert(host.online && host.ack_pan == 90);
    puts("PASS: leading LF in probe resynchronizes UNO partial input");

    reset(UINT32_MAX - 300);
    run_for(2000);
    assert(host.online && host.timeout_count == 0);
    robot_link_pose(&host, 95, 100);
    run_for(1000);
    assert(host.ack_pan == 95 && host.ack_tilt == 100);
    puts("PASS: 32-bit millisecond wraparound");

    reset(); run_for(600);
    robot_link_pose(&host, 110, 120);
    char command[40];
    clock_ms += 200;
    assert(robot_link_poll(&host, clock_ms, command, sizeof(command)) > 0);
    feed("OK,PAN=111,TILT=120\n");
    assert(!host.online && host.error_count == 1);
    run_for(2000);
    assert(host.online);
    puts("PASS: wrong target ACK is rejected and link re-handshakes");

    reset(); run_for(600);
    robot_link_pose(&host, 110, 120);
    clock_ms += 200;
    assert(robot_link_poll(&host, clock_ms, command, sizeof(command)) > 0);
    clock_ms += ROBOT_REPLY_MS;
    feed("OK,PAN=110,TILT=120\n");
    assert(!host.online && host.timeout_count == 1);
    puts("PASS: expired ACK cannot mark link online");

    reset();
    const std::string binary = std::string("PAN=120,TILT=100") + char(0) + "garbage\n";
    uno_input(binary);
    assert(panTarget == 90 && tiltTarget == 90);
    assert(edgiSerial.output.find("ERR,FORMAT") != std::string::npos);
    edgiSerial.output.clear();
    uno_input("PAN=1\r20,TILT=100\nPAN=120,TILT=100\r\r\n");
    assert(panTarget == 90 && tiltTarget == 90);
    uno_input("PAN=95,TILT=100\r\n");
    assert(panTarget == 95 && tiltTarget == 100);
    puts("PASS: UNO rejects NUL and embedded/repeated CR; accepts CRLF");

    reset();
    uno_input("PAN=120,TILT=");
    clock_ms += 151;
    uno_input("100\n");
    assert(panTarget == 90 && tiltTarget == 90);
    assert(edgiSerial.output.find("ERR,FORMAT") != std::string::npos);
    uno_input("PAN=95,TILT=100\n");
    assert(panTarget == 95 && tiltTarget == 100);
    puts("PASS: UNO discards stale partial command and recovers at LF");

    reset();
    for (char c : std::string("PAN=120,TILT=100\n")) edgiSerial.input.push_back(c);
    edgiSerial.overflow_flag = true;
    loop();
    assert(panTarget == 90 && edgiSerial.input.empty());
    uno_input("\nPING\n");
    assert(edgiSerial.output.find("ERR,OVERFLOW") != std::string::npos);
    assert(edgiSerial.output.find("PONG,") != std::string::npos);
    puts("PASS: UNO software UART overflow flushes buffer and resynchronizes");

    reset(); run_for(600);
    robot_link_pose(&host, 100, 100);
    clock_ms += 200;
    assert(robot_link_poll(&host, clock_ms, command, sizeof(command)) > 0);
    ack_before = host.ack_count;
    feed("OK,PAN=1\r00,TILT=100\nOK,PAN=100,TILT=100\r\r\n");
    assert(host.ack_count == ack_before && host.pending == ROBOT_WAIT_POSE);
    feed("OK,PAN=100,TILT=100\r\n");
    assert(host.ack_count == ack_before + 1 && host.ack_pan == 100);
    puts("PASS: M55 rejects embedded/repeated CR without accepting forged ACK");

    reset();
    uno_input("PAN=120,TILT=120\n");
    const unsigned long valid_at = lastValidCommandMs;
    for (int i = 0; i < 55; ++i) {
        clock_ms += 100;
        uno_input("PAN=180,TILT=180\n");
    }
    assert(lastValidCommandMs == valid_at && timeoutCenterApplied);
    assert(panTarget == 90 && tiltTarget == 90);
    puts("PASS: invalid traffic does not keep the UNO watchdog alive");
    return 0;
}
