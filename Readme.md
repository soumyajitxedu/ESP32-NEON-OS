### Project Overview

An embedded operating system and gaming platform engineered for ESP32 and Arduino microcontrollers driving SSD1306 OLED displays ($128 \times 64$). The system blends interactive arcade gaming with dynamic display management, delivering responsive, double-buffered graphics using the `Adafruit_SSD1306` and `Adafruit_GFX` framework.

---

### Key Capabilities

* **Arcade Game Engine (`GAME.h`)**:
* **Space Shooter**: Features multi-layer parallax starfield scrolling, retro particle effects, dynamic bullet collision physics, scaling enemy waves, real-time HUD display, and buzzer audio cues.
* **Pong**: Single-player gameplay against an AI CPU featuring trajectory forecasting, physics-based paddle deflections, real-time court rendering, and score tracking.
* **Arcade Menu**: Animated selection interface set against an active starfield background.


* **OS & Display Features**:
* **Dynamic Engine**: Supports high-detail custom bitmap backgrounds (e.g., Tanjiro), procedural textures (Weave, Dither, Noise, Hatch), and animated live scenes (Cat Night with procedural tail and cloud movement).
* **Input System**: Non-blocking button debouncer handling short presses, continuous directional navigation, and long-press hold gestures (`BACK` command).
* **Audio Pipeline**: Piezo buzzer output for interactive menu feedback, collisions, firing events, and game state transitions.
