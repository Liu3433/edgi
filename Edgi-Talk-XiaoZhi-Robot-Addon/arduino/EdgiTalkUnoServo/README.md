# Edgi-Talk UNO servo controller

This Arduino IDE sketch is the motion-execution layer for the Edgi-Talk robot. The matching M55 UART and Xiaozhi MCP tools are implemented.

**中文接线图、烧录顺序、小智 AI 口令和排错：** [ROBOT_AI_QUICKSTART.md](../../Ai_Read/ROBOT_AI_QUICKSTART.md)。

## Upload target

- Board: **Arduino Uno** (ATmega328P)
- Sketch: `EdgiTalkUnoServo.ino`
- Libraries: `SoftwareSerial` from Arduino AVR Boards, and Arduino `Servo`. Install Servo through Library Manager if `Servo.h` is missing.

Disconnect the level-shifter UART wires from D2/D3 only if they prevent sketch upload. D0/D1 are not used.

## Wiring

| Function | UNO | HW-221 / external connection |
|---|---:|---|
| Receive Edgi-Talk command | D2 | B1; corresponding A1 is Edgi-Talk TX |
| Send status to Edgi-Talk | D3 | B2; corresponding A2 is Edgi-Talk RX |
| Pan servo signal | D9 | Pan SG996 signal wire |
| Tilt servo signal | D10 | Tilt SG996 signal wire |
| Logic low-voltage rail | — | HW-221 VCCA = Edgi-Talk 3.3 V |
| Logic high-voltage rail | — | HW-221 VCCB = 5 V |
| Ground | GND | Common to Edgi-Talk, HW-221, and servo PSU |

OE is tied to Edgi 3.3 V in the supplied wiring; this firmware does not drive a GPIO for OE. For controlled power transitions, hold OE low until both rails are stable; see the Chinese guide for the hardware limitation.

Power the servos from a separate supply with voltage and current matched to the actual servo specification. Never power them through the UNO 5 V pin or Edgi-Talk header. The servo supply GND must be common. Confirm the expansion board V rail is isolated from UNO 5 V before attaching an external servo supply.

## UART protocol, version 1

The UART uses **38400, 8N1**, matching M55. SoftwareSerial and Servo can still interfere with interrupt timing; this rate does not guarantee jitter-free operation. Physical verification is required.

Every command is ASCII and newline terminated. Do not insert spaces. A pose command has the exact field order `PAN=<integer>,TILT=<integer>`.

```text
PAN=90,TILT=120\n
CENTER\n
PING\n
```

`PAN` and `TILT` must appear together. Initial software limits are Pan `20..160` and Tilt `30..150`; they are not mechanically calibrated. Adjust both this sketch and M55 `applications/robot/robot_link.h` after checking the mechanism.

Responses are newline-terminated:

```text
READY,PROTO=1,PAN=90,TILT=90
OK,PAN=90,TILT=120
PONG,PAN=90,TILT=120
ERR,FORMAT
ERR,RANGE
ERR,OVERFLOW
SAFE,CENTER,PAN=90,TILT=90
```

The reported pose is the commanded target, not a measured servo position. The servos move at up to two degrees every 20 ms. A five-second interval without a valid command or `PING` causes a smooth move to center and emits `SAFE,CENTER,...` once.

Parser hardening (2026-10-28): NUL, binary data, embedded/repeated CR are rejected; only LF/CRLF terminate a frame. Partial frames with a gap over 150 ms are discarded through LF. SoftwareSerial overflow flushes the buffer and waits for LF to resynchronize. Invalid commands do not refresh the watchdog. Protocol 1 has no CRC or sequence number and cannot detect every valid-looking corruption.

## Bench test sequence

1. Remove the servo horns or ensure their movement path is clear.
2. Flash the UNO sketch and wire the logic side with all grounds common.
3. Use the matching M55 firmware (`robot status`, then `robot auto off`, `robot center` from the M55 debug terminal). The UNO USB Serial Monitor is not the D2/D3 command channel.
4. Verify `PONG` / `OK` replies, then test small changes such as `PAN=95,TILT=90`.
5. Attach horns only after confirming direction and safe range. Tune `PAN_*_DEG` and `TILT_*_DEG` for the real mechanical stops.

M55 uses uart5 (CN5 physical TX=8, RX=10), while uart2 remains the debug console. M33 must release uart5. The M55 handshake sends `\nPING\n`, requires PONG, sends CENTER and requires matching OK before setting online. READY alone is not a completed handshake. Idle heartbeat is one PING per second. See the Chinese guide for flash files and voice instructions.
