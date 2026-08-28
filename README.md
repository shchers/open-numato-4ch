# Numato-Style USB Relay Emulator (Arduino UNO + Seeed Relay Shield v3.0)

Firmware that turns an Arduino UNO R3 + Seeed Studio Relay Shield v3.0 into a
USB-serial controlled 4-channel relay module, using the same plain-text
command set as Numato's USB Relay Modules. Existing Numato client scripts
(pyserial, Node serialport, terminal emulators, etc.) that talk to the device
over a COM/tty port will work against this firmware without modification.

## 1. Architecture

```
 Host PC                         Arduino UNO R3
┌─────────────┐   USB (CDC/ACM)  ┌────────────────────────────────┐
│ Terminal /   │◄────────────────►│ ATmega328P                    │
│ Numato client│   serial, 9600   │  - USB-to-serial via ATmega16U2│
│ script       │   8N1            │  - Command parser (loop())     │
└─────────────┘                  │  - EEPROM (device ID storage)  │
                                  │  - D4-D7 digital outputs       │
                                  └───────────┬────────────────────┘
                                              │ D4  D5  D6  D7
                                              ▼   ▼   ▼   ▼
                                  ┌────────────────────────────────┐
                                  │ Seeed Relay Shield v3.0         │
                                  │  RELAY1..RELAY4 (active HIGH)   │
                                  │  onboard LED per relay          │
                                  │  COM/NO/NC screw terminals      │
                                  └────────────────────────────────┘
```

![Relay Hat](https://files.seeedstudio.com/wiki/Relay_Shield_v3.0/img/Relay_Shield_v3.0.png)

**Software layers inside the sketch:**

1. **Transport layer (`loop()`)** — reads raw bytes from `Serial`, echoes
   each byte back (so the device behaves like a real terminal-driven Numato
   module), and assembles bytes into a line buffer until `\r` or `\n`.
2. **Command dispatcher (`processCommand`)** — tokenizes the completed line
   on spaces and routes to a handler based on the first token (`relay`,
   `id`, `ver`, `reset`, `help`).
3. **Command handlers** (`handleRelayCommand`, `handleIdCommand`) — parse
   remaining tokens, validate arguments, and act on hardware state.
4. **Hardware abstraction** — a fixed array `relayPins[4] = {4,5,6,7}` maps
   logical relay index 0-3 to the shield's physical control pins; all
   relay reads/writes go through `digitalRead`/`digitalWrite` on this array,
   so the pin mapping is the only shield-specific detail in the code.
5. **Persistence** — the 8-character device ID is stored in the ATmega's
   internal EEPROM (`EEPROM.h`), initialized to `"00000000"` on first boot.

**Command protocol** (see the firmware file's header comment for the full
list): `relay on/off/read <0-3>`, `relay readall`, `relay writeall <hex>`,
`id get/set`, `ver`, `reset`, `help`. Every response ends with `\r\n>` so
clients can detect the end of a reply the same way they do with genuine
Numato modules.

## 2. Supported Numato API

This firmware implements the subset of the Numato USB Relay Module command
API that applies to a 4-channel board. It's a drop-in match for scripts
written against the real [Numato 4 Channel USB Relay Module](https://numato.com/product/4-channel-usb-relay-module),
as long as they talk to it as a plain serial/COM port rather than by USB
vendor/product ID.

| Command | Description | Reply |
|---|---|---|
| `relay on <n>` | Energize relay `n` (0-3) | none (just `\r\n`) |
| `relay off <n>` | De-energize relay `n` (0-3) | none (just `\r\n`) |
| `relay read <n>` | Query relay `n` state | `on` or `off` |
| `relay readall` | Query all relay states at once | single hex digit bitmask (bit0 = relay 0) |
| `relay writeall <hex>` | Set all relays from a hex digit bitmask | none (just `\r\n`) |
| `id get` | Read the 8-character device ID | 8-character string |
| `id set <8 chars>` | Store a new 8-character device ID (EEPROM) | none (just `\r\n`) |
| `ver` | Read firmware version | version string |
| `reset` | Turn all relays off | none (just `\r\n`) |
| `help` / `?` | List supported commands | command list |

Not implemented (present on some larger Numato modules, not applicable to a
4-relay board): GPIO commands (`gpio set/clear/read/...`), ADC commands
(`adc read`), and multi-word device ID formats used on larger relay/GPIO
modules. Relay indices 0-9 as well as the `A`-`V` letter form (Numato's
convention for channels beyond 9) are both accepted by the parser, though
only 0-3 are valid on this board.

## 3. Hardware

| Item | Detail |
|---|---|
| Board | Arduino UNO R3 |
| Shield | Seeed Studio Relay Shield v3.0 |
| Relay control pins | D4, D5, D6, D7 (fixed by the shield) |
| Relay logic | Active HIGH (HIGH = COM↔NO connected / energized) |
| Status LEDs | Onboard the shield, wired to each relay coil — no extra code needed |
| Power | USB power is enough to drive the shield's relay coils for typical use |

## 4. Prerequisites for command-line build/flash

Install `arduino-cli` (no Arduino IDE required):

```bash
# Linux / macOS
curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh
# puts the binary in ./bin — move it onto your PATH, e.g.:
sudo mv ./bin/arduino-cli /usr/local/bin/
```

```powershell
# Windows (PowerShell)
winget install ArduinoSA.CLI
```

Verify installation:

```bash
arduino-cli version
```

Install the AVR core (needed for the UNO's ATmega328P):

```bash
arduino-cli core update-index
arduino-cli core install arduino:avr
```

## 5. Project layout

Put the sketch in a folder with the **same name** as the `.ino` file —
`arduino-cli` requires this:

```
numato_relay_emulator/
└── numato_relay_emulator.ino
```

```bash
mkdir -p numato_relay_emulator
mv numato_relay_emulator.ino numato_relay_emulator/
```

## 6. Build (compile)

```bash
arduino-cli compile --fqbn arduino:avr:uno numato_relay_emulator
```

- `--fqbn arduino:avr:uno` selects the UNO R3 board profile.
- On success you'll see sketch size output (flash/SRAM usage); this sketch
  is small and easily fits the UNO's 32 KB flash / 2 KB SRAM.

To build with verbose output (useful for debugging include/path issues):

```bash
arduino-cli compile --fqbn arduino:avr:uno --verbose numato_relay_emulator
```

## 7. Identify the serial port

```bash
arduino-cli board list
```

Look for a line referencing the UNO, e.g.:

- Linux: `/dev/ttyACM0` or `/dev/ttyUSB0`
- macOS: `/dev/cu.usbmodemXXXX`
- Windows: `COM3` (or similar)

## 8. Flash (upload)

```bash
arduino-cli upload -p /dev/ttyACM0 --fqbn arduino:avr:uno numato_relay_emulator
```

Replace `/dev/ttyACM0` with the port from step 6 (e.g. `COM3` on Windows).

Combined compile + upload in one step:

```bash
arduino-cli compile --fqbn arduino:avr:uno numato_relay_emulator && \
arduino-cli upload -p /dev/ttyACM0 --fqbn arduino:avr:uno numato_relay_emulator
```

## 9. Verify over serial

Using `arduino-cli`'s built-in monitor:

```bash
arduino-cli monitor -p /dev/ttyACM0 --config baudrate=9600
```

Or with any terminal tool, 9600 8N1, line ending set to CR or CRLF. Try:

```
relay on 0
relay read 0
relay readall
relay off 0
```

Expected behavior: each command is echoed back, followed by the result
(if any) and a `>` prompt, e.g.:

```
>relay on 0
>relay read 0
on
>
```

## 10. Troubleshooting

| Symptom | Likely cause / fix |
|---|---|
| `arduino-cli board list` shows nothing | Try a different USB cable/port; on Linux check `dmesg` for enumeration errors |
| Upload fails with a timeout | Close any other program (Serial Monitor, IDE) holding the port open |
| Permission denied on `/dev/ttyACM0` | Linux: add your user to the `dialout` group (`sudo usermod -aG dialout $USER`, then log out/in) |
| Relays don't switch | Confirm the shield is fully seated on the UNO header pins; check D4-D7 aren't used by other wiring |
| No response over serial | Confirm baud rate is 9600 and line ending is CR or LF |

## 11. References

- [Numato 4 Channel USB Relay Module](https://numato.com/product/4-channel-usb-relay-module) — the reference device whose command API this firmware emulates
- [Arduino UNO R3](https://store.arduino.cc/products/arduino-uno-rev3) — the board this sketch targets
- [Seeed Studio Relay Shield v3.0](https://www.seeedstudio.com/Relay-Shield-v3-0.html) — the relay hardware ("hat") driven by D4-D7

## 12. License

Licensed under the Apache License, Version 2.0. See the [LICENSE](LICENSE)
file for the full text. Each source file carries the standard Apache 2.0
header identifying it as covered by this license.
