/*
 * Copyright 2026 Sergii Shcherbakov
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */

/*
 * Numato-style 4-Channel USB Relay Module emulator
 * ---------------------------------------------------------------
 * Hardware:
 *   - Arduino UNO R3
 *   - Seeed Studio Relay Shield v3.0
 *
 * Relay Shield v3.0 wiring (fixed by the shield, not configurable):
 *   RELAY1 -> D4
 *   RELAY2 -> D5
 *   RELAY3 -> D6
 *   RELAY4 -> D7
 *   Relays are ACTIVE HIGH: pin HIGH  -> COM connected to NO (energized)
 *                           pin LOW   -> COM connected to NC (de-energized)
 *   Each relay has its own onboard LED tied to its coil, so no extra
 *   status-LED wiring or code is needed.
 *
 * Command protocol (mirrors Numato's USB relay modules, e.g. the
 * 4/8/16-Channel USB Relay Module):
 *
 *   relay on <n>        turn relay n ON               (n = 0..3)
 *   relay off <n>        turn relay n OFF               (n = 0..3)
 *   relay read <n>       reply "on" or "off"            (n = 0..3)
 *   relay readall        reply single hex digit bitmask (bit0 = relay0)
 *   relay writeall <hex> set all relays from a hex digit bitmask
 *   id get                reply the 8-character device ID
 *   id set <8 chars>      store a new 8-character device ID (EEPROM)
 *   ver                   reply firmware version string
 *   reset                 turn all relays off
 *   help / ?              list supported commands
 *
 * Commands are terminated with CR and/or LF. The device echoes every
 * received character (as the real hardware does when used with a
 * terminal emulator) and finishes each response with "\r\n>" so that
 * existing Numato client scripts (pyserial, etc.) that look for the
 * trailing '>' prompt work unmodified.
 *
 * Relay numbers 0-9 may also be given as A-V per the Numato spec for
 * modules with more than 10 channels; harmless here since only 0-3
 * are valid, but it keeps the parser drop-in compatible.
 */

#include <EEPROM.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

const uint8_t NUM_RELAYS = 4;
const uint8_t relayPins[NUM_RELAYS] = {4, 5, 6, 7};

const int EEPROM_ID_ADDR = 0;
const int EEPROM_ID_LEN  = 8;

char cmdBuffer[64];
uint8_t cmdIndex = 0;

// ---- forward declarations ----
void processCommand(char *line);
void handleRelayCommand();
void handleIdCommand();
int  parseRelayIndex(char *arg);
void printPrompt();
void printHelp();

void setup() {
  Serial.begin(9600);

  for (uint8_t i = 0; i < NUM_RELAYS; i++) {
    pinMode(relayPins[i], OUTPUT);
    digitalWrite(relayPins[i], LOW);
  }

  // Initialize a default ID in EEPROM the first time the sketch runs
  // on a fresh chip (erased EEPROM reads as 0xFF).
  if ((uint8_t)EEPROM.read(EEPROM_ID_ADDR) == 0xFF) {
    const char defaultId[EEPROM_ID_LEN] = {'0','0','0','0','0','0','0','0'};
    for (int i = 0; i < EEPROM_ID_LEN; i++) {
      EEPROM.write(EEPROM_ID_ADDR + i, defaultId[i]);
    }
  }

  Serial.print("\r\n>");
}

void loop() {
  while (Serial.available() > 0) {
    char c = Serial.read();

    // Local echo, matching how the real module behaves in a terminal
    Serial.write(c);

    if (c == '\r' || c == '\n') {
      cmdBuffer[cmdIndex] = '\0';
      if (cmdIndex > 0) {
        processCommand(cmdBuffer);
      }
      cmdIndex = 0;
      printPrompt();
    } else if (c == 8 || c == 127) {          // backspace / delete
      if (cmdIndex > 0) cmdIndex--;
    } else if (cmdIndex < sizeof(cmdBuffer) - 1) {
      cmdBuffer[cmdIndex++] = c;
    }
  }
}

void printPrompt() {
  Serial.print("\r\n>");
}

void processCommand(char *line) {
  while (*line == ' ') line++;               // trim leading spaces

  char *cmd = strtok(line, " ");
  if (cmd == NULL) return;

  if (strcasecmp(cmd, "relay") == 0) {
    handleRelayCommand();
  } else if (strcasecmp(cmd, "id") == 0) {
    handleIdCommand();
  } else if (strcasecmp(cmd, "ver") == 0) {
    Serial.print("\r\n00000001");
  } else if (strcasecmp(cmd, "reset") == 0) {
    for (uint8_t i = 0; i < NUM_RELAYS; i++) digitalWrite(relayPins[i], LOW);
    Serial.print("\r\n");
  } else if (strcasecmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) {
    printHelp();
  } else {
    Serial.print("\r\nInvalid command");
  }
}

void handleRelayCommand() {
  char *sub = strtok(NULL, " ");
  if (sub == NULL) { Serial.print("\r\nInvalid command"); return; }

  if (strcasecmp(sub, "on") == 0) {
    int r = parseRelayIndex(strtok(NULL, " "));
    if (r < 0) { Serial.print("\r\nInvalid relay number"); return; }
    digitalWrite(relayPins[r], HIGH);
    Serial.print("\r\n");

  } else if (strcasecmp(sub, "off") == 0) {
    int r = parseRelayIndex(strtok(NULL, " "));
    if (r < 0) { Serial.print("\r\nInvalid relay number"); return; }
    digitalWrite(relayPins[r], LOW);
    Serial.print("\r\n");

  } else if (strcasecmp(sub, "read") == 0) {
    int r = parseRelayIndex(strtok(NULL, " "));
    if (r < 0) { Serial.print("\r\nInvalid relay number"); return; }
    Serial.print("\r\n");
    Serial.print(digitalRead(relayPins[r]) == HIGH ? "on" : "off");

  } else if (strcasecmp(sub, "readall") == 0) {
    uint8_t mask = 0;
    for (uint8_t i = 0; i < NUM_RELAYS; i++) {
      if (digitalRead(relayPins[i]) == HIGH) mask |= (1 << i);
    }
    char hex[2];
    sprintf(hex, "%01X", mask);
    Serial.print("\r\n");
    Serial.print(hex);

  } else if (strcasecmp(sub, "writeall") == 0) {
    char *arg = strtok(NULL, " ");
    if (arg == NULL || strlen(arg) != 1 || !isxdigit((unsigned char)arg[0])) {
      Serial.print("\r\nInvalid mask");
      return;
    }
    uint8_t mask = (uint8_t)strtol(arg, NULL, 16);
    for (uint8_t i = 0; i < NUM_RELAYS; i++) {
      digitalWrite(relayPins[i], (mask & (1 << i)) ? HIGH : LOW);
    }
    Serial.print("\r\n");

  } else {
    Serial.print("\r\nInvalid command");
  }
}

void handleIdCommand() {
  char *sub = strtok(NULL, " ");
  if (sub == NULL) { Serial.print("\r\nInvalid command"); return; }

  if (strcasecmp(sub, "get") == 0) {
    char id[EEPROM_ID_LEN + 1];
    for (int i = 0; i < EEPROM_ID_LEN; i++) id[i] = EEPROM.read(EEPROM_ID_ADDR + i);
    id[EEPROM_ID_LEN] = '\0';
    Serial.print("\r\n");
    Serial.print(id);

  } else if (strcasecmp(sub, "set") == 0) {
    char *arg = strtok(NULL, " ");
    if (arg == NULL || strlen(arg) != EEPROM_ID_LEN) {
      Serial.print("\r\nID must be 8 characters");
      return;
    }
    for (int i = 0; i < EEPROM_ID_LEN; i++) EEPROM.write(EEPROM_ID_ADDR + i, arg[i]);
    Serial.print("\r\n");

  } else {
    Serial.print("\r\nInvalid command");
  }
}

// Accepts '0'-'3' or 'A'-'V' (Numato convention for channels > 9),
// but only 0-3 are valid on this 4-relay board.
int parseRelayIndex(char *arg) {
  if (arg == NULL || strlen(arg) != 1) return -1;
  char c = toupper((unsigned char)arg[0]);
  int idx;
  if (c >= '0' && c <= '9') idx = c - '0';
  else if (c >= 'A' && c <= 'V') idx = 10 + (c - 'A');
  else return -1;
  if (idx < 0 || idx >= NUM_RELAYS) return -1;
  return idx;
}

void printHelp() {
  Serial.print("\r\nCommands:");
  Serial.print("\r\n  relay on <0-3>");
  Serial.print("\r\n  relay off <0-3>");
  Serial.print("\r\n  relay read <0-3>");
  Serial.print("\r\n  relay readall");
  Serial.print("\r\n  relay writeall <hex 0-F>");
  Serial.print("\r\n  id get");
  Serial.print("\r\n  id set <8 chars>");
  Serial.print("\r\n  ver");
  Serial.print("\r\n  reset");
}
