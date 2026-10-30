/*
 * Edgi-Talk UNO servo controller
 *
 * UART wiring through HW-221 (TXS0108E):
 *   Edgi-Talk TX (3.3 V) -> A1; B1 -> UNO D2 (RX)
 *   UNO D3 (TX) -> B2; A2 -> Edgi-Talk RX (3.3 V)
 *   VCCA=3.3 V, VCCB=5 V, and all grounds are common.
 *
 * Servo wiring:
 *   D9  -> Pan servo signal
 *   D10 -> Tilt servo signal
 *   Servo power must use a separate supply matched to the actual servo rating.
 *   Connect the servo supply GND to UNO GND and Edgi-Talk GND.
 *
 * Protocol: ASCII, 38400 baud, 8N1, newline terminated.
 *   Host -> UNO: PAN=<0..180>,TILT=<0..180>\n
 *   Host -> UNO: CENTER\n
 *   Host -> UNO: PING\n
 *   UNO -> Host: READY,PROTO=1,PAN=90,TILT=90\n
 *   UNO -> Host: OK,PAN=<target>,TILT=<target>\n
 *   UNO -> Host: PONG,PAN=<target>,TILT=<target>\n
 *   UNO -> Host: ERR,FORMAT | ERR,RANGE | ERR,OVERFLOW\n
 *
 * Commands must contain both PAN and TILT so that every accepted command is
 * a complete pose.  PAN/TILT values are constrained further by the initial
 * software limits below.  Edit those four limits only after mechanically
 * checking the assembled robot.
 */

#include <Servo.h>
#include <SoftwareSerial.h>
#include <string.h>
#include <stdlib.h>

namespace {

const uint8_t EDGI_RX_PIN = 2;  // UNO RX: receives data from Edgi-Talk TX
const uint8_t EDGI_TX_PIN = 3;  // UNO TX: sends data to Edgi-Talk RX
const uint8_t PAN_SERVO_PIN = 9;
const uint8_t TILT_SERVO_PIN = 10;

// Must match M55. SoftwareSerial can still delay Servo interrupts; verify on hardware.
const unsigned long EDGI_BAUD = 38400UL;

// Tune only after checking the physical mechanism with the servos unloaded.
const int PAN_MIN_DEG = 20;
const int PAN_MAX_DEG = 160;
const int TILT_MIN_DEG = 30;
const int TILT_MAX_DEG = 150;
const int PAN_CENTER_DEG = 90;
const int TILT_CENTER_DEG = 90;

const unsigned long SERVO_UPDATE_MS = 20UL;
const unsigned long COMMAND_TIMEOUT_MS = 5000UL;
const uint8_t MAX_STEP_PER_UPDATE = 2;
const size_t RX_LINE_CAPACITY = 48;
const unsigned long RX_GAP_TIMEOUT_MS = 150UL;

SoftwareSerial edgiSerial(EDGI_RX_PIN, EDGI_TX_PIN);
Servo panServo;
Servo tiltServo;

char rxLine[RX_LINE_CAPACITY];
size_t rxLength = 0;
bool rxOverflow = false;
bool rxInvalid = false;
bool rxSawCR = false;
unsigned long lastRxByteMs = 0;

int panCurrent = PAN_CENTER_DEG;
int tiltCurrent = TILT_CENTER_DEG;
int panTarget = PAN_CENTER_DEG;
int tiltTarget = TILT_CENTER_DEG;
unsigned long lastServoUpdateMs = 0;
unsigned long lastValidCommandMs = 0;
bool timeoutCenterApplied = false;

bool inRange(int value, int minValue, int maxValue)
{
  return value >= minValue && value <= maxValue;
}

int moveToward(int current, int target)
{
  if (current < target) {
    current += MAX_STEP_PER_UPDATE;
    return current > target ? target : current;
  }
  if (current > target) {
    current -= MAX_STEP_PER_UPDATE;
    return current < target ? target : current;
  }
  return current;
}

void writeCurrentPose()
{
  panServo.write(panCurrent);
  tiltServo.write(tiltCurrent);
}

void sendPose(const __FlashStringHelper *prefix)
{
  edgiSerial.print(prefix);
  edgiSerial.print(F(",PAN="));
  edgiSerial.print(panTarget);
  edgiSerial.print(F(",TILT="));
  edgiSerial.println(tiltTarget);
}

void sendError(const __FlashStringHelper *reason)
{
  edgiSerial.print(F("ERR,"));
  edgiSerial.println(reason);
}

bool parsePose(const char *line, int &pan, int &tilt)
{
  static const char panPrefix[] = "PAN=";
  static const char tiltPrefix[] = ",TILT=";
  if (strncmp(line, panPrefix, sizeof(panPrefix) - 1) != 0) {
    return false;
  }

  const char *number = line + sizeof(panPrefix) - 1;
  if (*number < '0' || *number > '9') {
    return false;
  }
  char *end = NULL;
  long parsed = strtol(number, &end, 10);
  if (end == number || strncmp(end, tiltPrefix, sizeof(tiltPrefix) - 1) != 0 ||
      parsed < 0 || parsed > 180) {
    return false;
  }

  number = end + sizeof(tiltPrefix) - 1;
  if (*number < '0' || *number > '9') {
    return false;
  }
  long secondParsed = strtol(number, &end, 10);
  if (end == number || *end != '\0' || secondParsed < 0 || secondParsed > 180) {
    return false;
  }

  pan = (int)parsed;
  tilt = (int)secondParsed;
  return true;
}

void acceptPose(int pan, int tilt)
{
  panTarget = pan;
  tiltTarget = tilt;
  lastValidCommandMs = millis();
  timeoutCenterApplied = false;
  sendPose(F("OK"));
}

void processLine(char *line)
{
  if (strcmp(line, "PING") == 0) {
    lastValidCommandMs = millis();
    timeoutCenterApplied = false;
    sendPose(F("PONG"));
    return;
  }

  if (strcmp(line, "CENTER") == 0) {
    acceptPose(PAN_CENTER_DEG, TILT_CENTER_DEG);
    return;
  }

  int pan = 0;
  int tilt = 0;
  if (!parsePose(line, pan, tilt)) {
    sendError(F("FORMAT"));
    return;
  }

  if (!inRange(pan, PAN_MIN_DEG, PAN_MAX_DEG) ||
      !inRange(tilt, TILT_MIN_DEG, TILT_MAX_DEG)) {
    sendError(F("RANGE"));
    return;
  }

  acceptPose(pan, tilt);
}

void pollEdgiSerial()
{
  // A lost byte makes the entire current frame untrustworthy.
  if (edgiSerial.overflow()) {
    // The ring may contain bytes from before AND after the lost byte. Flush it
    // and resynchronize at a future LF, never execute a surviving suffix.
    uint8_t remaining = 64;
    while (remaining-- && edgiSerial.available() > 0) edgiSerial.read();
    rxLength = 0;
    rxSawCR = false;
    rxOverflow = true;
    return;
  }
  // Bound work per loop so malformed traffic cannot starve slew/failsafe checks.
  uint8_t budget = 64;
  while (budget-- && edgiSerial.available() > 0) {
    const char c = (char)edgiSerial.read();
    const unsigned long now = millis();
    if ((rxLength || rxSawCR) && now - lastRxByteMs > RX_GAP_TIMEOUT_MS)
      rxInvalid = true;
    lastRxByteMs = now;

    if (c == '\r') {
      if (rxSawCR) rxInvalid = true;
      rxSawCR = true;
      continue;
    }
    if (c == '\n') {
      if (rxOverflow) {
        sendError(F("OVERFLOW"));
      } else if (rxInvalid) {
        sendError(F("FORMAT"));
      } else if (rxLength > 0) {
        rxLine[rxLength] = '\0';
        processLine(rxLine);
      }
      rxLength = 0;
      rxOverflow = false;
      rxInvalid = false;
      rxSawCR = false;
      continue;
    }

    // Reject embedded CR, NUL and binary bytes, rather than executing a prefix.
    if (rxSawCR || (unsigned char)c < 32 || (unsigned char)c > 126)
      rxInvalid = true;
    if (rxOverflow || rxInvalid) {
      continue;
    }
    if (rxLength + 1 >= RX_LINE_CAPACITY) {
      rxOverflow = true;
      continue;
    }
    rxLine[rxLength++] = c;
  }
}

void updateServos()
{
  const unsigned long now = millis();
  if (now - lastServoUpdateMs < SERVO_UPDATE_MS) {
    return;
  }
  lastServoUpdateMs = now;

  const int nextPan = moveToward(panCurrent, panTarget);
  const int nextTilt = moveToward(tiltCurrent, tiltTarget);
  if (nextPan != panCurrent || nextTilt != tiltCurrent) {
    panCurrent = nextPan;
    tiltCurrent = nextTilt;
    writeCurrentPose();
  }
}

void applyCommunicationFailsafe()
{
  const unsigned long now = millis();
  if (!timeoutCenterApplied && now - lastValidCommandMs >= COMMAND_TIMEOUT_MS) {
    panTarget = PAN_CENTER_DEG;
    tiltTarget = TILT_CENTER_DEG;
    timeoutCenterApplied = true;
    sendPose(F("SAFE,CENTER"));
  }
}

}  // namespace

void setup()
{
  rxLength = 0;
  rxOverflow = rxInvalid = rxSawCR = false;
  panCurrent = panTarget = PAN_CENTER_DEG;
  tiltCurrent = tiltTarget = TILT_CENTER_DEG;
  timeoutCenterApplied = false;
  lastServoUpdateMs = lastRxByteMs = millis();
  // Do not use Serial on D0/D1: USB upload/debug remains independent.
  edgiSerial.begin(EDGI_BAUD);

  panServo.attach(PAN_SERVO_PIN);
  tiltServo.attach(TILT_SERVO_PIN);
  writeCurrentPose();

  lastValidCommandMs = millis();
  edgiSerial.print(F("READY,PROTO=1,PAN="));
  edgiSerial.print(panTarget);
  edgiSerial.print(F(",TILT="));
  edgiSerial.println(tiltTarget);
}

void loop()
{
  pollEdgiSerial();
  applyCommunicationFailsafe();
  updateServos();
}
