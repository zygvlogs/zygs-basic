# 🚀 ZYGS-BASIC for ESP32

[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](https://opensource.org/licenses/MIT)
[![Platform: ESP32](https://img.shields.io/badge/Platform-ESP32-orange.svg)](https://www.espressif.com/en/products/socs/esp32)

A lightweight, feature-rich BASIC interpreter designed for the ESP32. Turn your microcontroller into a standalone programming environment accessible via any Serial Terminal (PuTTY, Minicom, etc.) with a nostalgic ZX Spectrum-style TUI.

---

## ✨ Features

* **Classic BASIC Workflow**: Line-numbered programming (`10 PRINT "HELLO"`) with immediate mode execution.
* **Improved Syntax**:

  * `GOSUB` / `RETURN` (returns to the line *after* the call).
  * String variables (e.g., `A$`, `NAME$`).
  * Complex logic with `AND` / `OR` in `IF` statements.
  * Multi-statement lines using colons (`:`).
* **Built-in Functions**: `ABS`, `RND`, `SQR`, `MIN`, `MAX`, `MILLIS`, `FREEMEM`.
* **Hardware Integration**: Directly control ESP32 GPIOs:

  * `PINMODE`, `PINOUT`, `ANAOUT` (PWM), `BLINK`.
  * `PININ`, `ANALIN` functions for reading inputs.
* **Nostalgic TUI**: A fixed input line at the top with a scrolling output area below, reminiscent of classic home computers.
* **Advanced Commands**: `RENUM` for re-ordering lines, `CLS` for screen clearing, and `MEM` for heap monitoring.

---

## 🖥️ Text User Interface (TUI)

The interpreter features an ANSI-based TUI. For the best experience, use a terminal like **PuTTY** or **Tera Term**:

* **Baud Rate**: `115200`
* **Terminal Type**: `VT100` or `xterm`
* **Local Echo**: Off (The interpreter handles echoing)

The screen is split:

* **Top Blue Bar**: Your current input line.
* **Main Area**: Program output, listing, and execution history.

---

## 📜 Language Reference

### Commands (Immediate Mode)

| Command                | Description                                           |
| :--------------------- | :---------------------------------------------------- |
| `LIST [n[-m]]`         | List program lines (optional range).                  |
| `RUN`                  | Execute the current program.                          |
| `NEW`                  | Clear the program and all variables.                  |
| `RENUM [start[,step]]` | Renumber program lines and update GOTO/GOSUB targets. |
| `VARS`                 | List all current variable values.                     |
| `FREE` / `MEM`         | Show available system heap memory.                    |
| `CLS`                  | Clear the terminal screen.                            |
| `HELP`                 | Show a quick reference guide.                         |

### Statements

* **Control**: `GOTO`, `GOSUB`, `RETURN`, `FOR..TO..STEP`, `NEXT`, `IF..THEN..ELSE`, `END`, `STOP`.
* **I/O**: `PRINT`, `INPUT ["prompt";] VAR`, `DELAY ms`.
* **Variables**: `LET` or implied assignment (`X = 10`, `A$ = "Hello"`).
* **Hardware**:

  * `PINMODE pin, OUT/IN/PULLUP`
  * `PINOUT pin, state`
  * `ANAOUT pin, val` (PWM 0-255)
  * `BLINK pin, ms`

### Functions

* **Math**: `ABS(n)`, `RND(n)`, `SQR(n)`, `MIN(a,b)`, `MAX(a,b)`.
* **System**: `MILLIS()`, `FREEMEM()`.
* **Hardware**: `PININ(n)`, `ANALIN(n)`.
* **Formatting**: `TAB(n)` (use inside `PRINT`).

---

## 🛠️ Installation (PlatformIO)

This project is built using [PlatformIO](https://platformio.org/).

1. **Install PlatformIO**: If you haven't already, install the PlatformIO IDE or CLI.
2. **Connect ESP32**: Plug your ESP32 board into your computer.
3. **Build and Upload**:

   ```bash
   # Build the project
   pio run

   # Upload to ESP32
   pio run -t upload

   # Open Serial Monitor
   pio device monitor -b 115200
   ```
4. **Configuration**: If your board is not a standard `esp32dev`, modify `platformio.ini` to match your hardware.

---

## 🚀 Quick Start Example

Type the following into the terminal to create a simple LED blinker, assuming LED on Pin 2:

```basic
10 PINMODE 2, OUT
20 PRINT "BLINKING LED ON PIN 2..."
30 PINOUT 2, 1
40 DELAY 500
50 PINOUT 2, 0
60 DELAY 500
70 GOTO 30
RUN
```

Press `Ctrl+C` or your terminal's break sequence to stop execution if necessary.

---

## ⚖️ License

Copyright (C) 2025 Zygimantas Mažeika / ZygMediaGroup

This project is licensed under the **MIT License**. See the [LICENSE](LICENSE) file for details.
