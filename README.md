
<div align="center">

![Demo GIF](https://github.com/soumyajitxedu/ESP32-NEON-OS/blob/main/assets/546740024-2ffc652a-f392-42e7-9a13-d7fb91f3770d.gif)

# ESP32 NEON OS

**A modular virtual pet and utility OS for the ESP32**

[![Arduino](https://camo.githubusercontent.com/09a9298c9f3d8477d5821b24240bc169cb2b4a7cd4047f4393e365e8f22f4b48/68747470733a2f2f696d672e736869656c64732e696f2f62616467652f2d41726475696e6f2d3030393739443f6c6f676f3d41726475696e6f266c6f676f436f6c6f723d7768697465)](https://github.com/soumyajitxedu/ESP32-NEON-OS)
![ESP32](https://img.shields.io/badge/ESP32-Espressif-E7352C?style=flat-square&logo=espressif&logoColor=white)
![Rust](https://img.shields.io/badge/Port%20of-Catode32-DEA584?style=flat-square&logo=rust&logoColor=white)
![License](https://img.shields.io/badge/License-MIT-blue?style=flat-square)
![Status](https://img.shields.io/badge/Status-Active-success?style=flat-square)

[Repository](https://github.com/soumyajitxedu/ESP32-NEON-OS) · [Report Issue](https://github.com/soumyajitxedu/ESP32-NEON-OS/issues) · [Credits](#contributions--credits)

</div>

---

## Gallery

<div align="center">

| | |
|:-:|:-:|
| <img src="https://github.com/soumyajitxedu/ESP32-NEON-OS/blob/main/assets/20261004_104534.jpg" width="280"/> | <img src="https://github.com/soumyajitxedu/ESP32-NEON-OS/blob/main/assets/20261004_104546.jpg" width="280"/> |
| *Physical build* | *Virtual pet* |
| <img src="https://github.com/soumyajitxedu/ESP32-NEON-OS/blob/main/assets/20261004_104706.jpg" width="280"/> | <img src="https://github.com/soumyajitxedu/ESP32-NEON-OS/blob/main/assets/20261004_104558.jpg" width="280"/> |
| *Dragon wallpaper* | *Wiring layout* |

</div>

---

## About

ESP32 NEON OS is a complete, self-contained operating system for the ESP32 microcontroller. It combines a radial home menu, a suite of utility apps, three classic games, and a fully-featured virtual pet inspired by the open-source Rust project [Catode32](https://github.com/moonbench/catode32).

The system runs on any standard ESP32 with 4MB flash, requiring only an SSD1306 OLED display and four tactile push buttons. Everything is written in C++ using the Arduino framework.

---

## Tech Stack

<div align="center">

| <img src="https://camo.githubusercontent.com/c57992ccd8e44fbfa43178e6de1ec1d0974559148afeb3044100e6fc9142311c/68747470733a2f2f74656368737461636b2d67656e657261746f722e76657263656c2e6170702f6370702d69636f6e2e737667" width="60"/> | <img src="https://camo.githubusercontent.com/09a9298c9f3d8477d5821b24240bc169cb2b4a7cd4047f4393e365e8f22f4b48/68747470733a2f2f696d672e736869656c64732e696f2f62616467652f2d41726475696e6f2d3030393739443f6c6f676f3d41726475696e6f266c6f676f436f6c6f723d7768697465" width="140"/> | <img src="https://cdn.jsdelivr.net/gh/devicons/devicon/icons/rust/rust-original.svg" width="60"/> |
|:-:|:-:|:-:|
| **C++** | **Arduino** | **Rust** |
| *Language* | *Framework* | *Port Source* |

</div>

---

## Features

| Category | Feature | Description |
|---|---|---|
| **Interface** | Radial Home Menu | Smooth rotating icon carousel |
| **Interface** | Non-blocking Input | Debounced buttons with long-press |
| **Interface** | Brightness Control | 4 discrete levels in flash |
| **Interface** | Screen Flip | 180° rotation support |
| **Interface** | Idle Sleep | Configurable timeout (5s–Never) |
| **Interface** | Wallpaper System | Static and animated boot wallpapers |
| **Utilities** | Persistent Settings | All preferences saved to NVS |
| **Utilities** | Factory Reset | Hold-to-confirm flash wipe |
| **Utilities** | Vault | 13 quotes with scrolling view |
| **Utilities** | Profile | Personal info with auto-scroll |
| **Utilities** | Game of Life | Conway's automaton with editing |
| **Utilities** | Screensaver | DVD Bounce, Radar, Concentric |
| **Utilities** | Timer | 5 presets with animated countdown |
| **Pet App** | 18-Stats System | Full stat-based simulation |
| **Pet App** | 26 Behaviors | Zoomies, Kneading, Hunting, etc. |
| **Pet App** | Complex Gardening | 3 pots, 4 seeds, 8 growth stages |
| **Pet App** | Dynamic Weather | Clear, Cloudy, Rain, Snow |
| **Pet App** | In-Game Store | Buy items with earned coins |
| **Pet App** | 3 Minigames | Dino Runner, Tetris, Snake |
| **Pet App** | NVS Persistence | Full state saved to flash |

---

## Applications

| # | App | Description |
|:-:|---|---|
| 1 | **Vault** | 13 curated quotes with scroll view |
| 2 | **Profile** | Personal info with auto-scroll |
| 3 | **Life** | Conway's Game of Life with cell editing |
| 4 | **Screensaver** | 3 burn-in prevention modes |
| 5 | **Timer** | Pomodoro timer with animated ring |
| 6 | **Sleep** | Manual display off until button press |
| 7 | **Cat Pet** | Flagship virtual pet application |

---

## Games

| Game | Description | Rewards |
|---|---|---|
| **Dino Runner** | Endless runner with jumping and ducking | Coins, Focus, Fitness |
| **Tetris** | Block-stacking with hard drop and levels | Coins, Focus, Playfulness |
| **Snake** | Smooth snake with particles and shake | Coins, Playfulness, Sociability |

---

## Wallpapers

| Wallpaper | Type | Details |
|---|---|---|
| **Tanjiro** | Static | Monochrome bitmap rendered at boot |
| **Cat Night** | Animated | Ear twitch, tail sway, twinkling stars, drifting clouds |

Select from **Settings → Wallpaper**. Saved to NVS.

---

## Hardware Requirements

| Component | Specification | Qty |
|---|---|---|
| Microcontroller | ESP32 (4MB flash) | 1 |
| Display | SSD1306 OLED, 128x64, I2C | 1 |
| Buttons | Tactile push, normally open | 4 |
| Switch | SPST toggle (battery) | 1 |
| Battery | LiPo 3.7V (optional) | 1 |
| Wires | Jumper wires (M-F) | ~12 |

---

## Wiring Layout

**Display (I2C):**

| SSD1306 | ESP32 |
|:---:|:---:|
| VCC | 3V3 |
| GND | GND |
| SDA | GPIO 21 |
| SCL | GPIO 22 |

**Buttons (internal pull-ups):**

| Button | ESP32 Pin |
|:---:|:---:|
| UP | GPIO 32 |
| DOWN | GPIO 33 |
| LEFT | GPIO 27 |
| RIGHT | GPIO 26 |

---

## Libraries

<div align="center">

| <img src="https://cdn.jsdelivr.net/gh/devicons/devicon/icons/arduino/arduino-original.svg" width="40"/> | <img src="https://cdn.jsdelivr.net/gh/devicons/devicon/icons/cplusplus/cplusplus-original.svg" width="40"/> | <img src="https://cdn.jsdelivr.net/gh/devicons/devicon/icons/c/c-original.svg" width="40"/> |
|:-:|:-:|:-:|
| **Adafruit SSD1306** | **Adafruit GFX** | **Wire** |
| **Preferences** | **math.h** | — |

</div>

| Library | Purpose |
|---|---|
| **Adafruit SSD1306** | OLED display driver |
| **Adafruit GFX** | Graphics primitives |
| **Wire** | I2C communication |
| **Preferences** | NVS flash storage |
| **math.h** | Sine/cosine for animations |

Install all via **Arduino IDE → Manage Libraries**.

---

## Setup Instructions

```bash
# 1. Clone the repository
git clone https://github.com/soumyajitxedu/ESP32-NEON-OS.git
```

**2. Install Arduino IDE** and add ESP32 board support:
```
https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
```

**3. Select board:** ESP32 Dev Module

**4. Install required libraries** via Library Manager

**5. Open** `Catode_NEON_ESP32.ino`

**6. Configure GPIO pins** if wired differently

**7. Upload** to your ESP32

**8. First boot:** Follow the adoption scene to select your cat

---

## Contributions & Credits

<div align="center">

| Contribution | Link |
|:-:|---|
| **Original Project** | [moonbench/catode32](https://github.com/moonbench/catode32) |
| **Secondary Reference** | [Mrmad092/Codes](https://github.com/Mrmad092/Codes) |
| **This Repository** | [soumyajitxedu/ESP32-NEON-OS](https://github.com/soumyajitxedu/ESP32-NEON-OS) |

</div>

Dragon wallpaper assets sourced from Pinterest.

---

## AI Models & Agents

<div align="center">

| Models | Agents |
|:-:|:-:|
| DeepSeek V4 | GPT Luna 6 |
| Claude Sonnet 5 | Sonnet 5.5 Agent |
| Claude Sonnet 5.5 | Gemini Pro 3.1 Agent |

</div>

---

## Development Timeline

| Milestone | Date |
|---|---|
| Project Started | August 24, 2026 |
| First Working Build | September 15, 2026 |
| Cat Pet Integration | September 28, 2026 |
| Complex Behaviors & Gardening | October 1, 2026 |
| Scene Integration & Polish | October 4, 2026 |
| **Total Active Development** | **8–12 hours** |

---

<div align="center">

**Built with C++, Arduino, and AI assistance**

*From August 24 to October 4, 2026*

</div>

---

## License

Provided as-is for educational and hobbyist use. All original Catode32 assets remain property of their respective creators.


---
