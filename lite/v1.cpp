// ============================================================
//  NEON OS Lite v4.1 — Single File (Debug + Diagnostics)
//  ESP32 + SSD1306 128x64 + 4 buttons
//  GPIO 32=UP 33=DOWN 27=LEFT(long=BACK) 26=RIGHT
//  Buzzer=4  Battery ADC=34 (optional 2:1 divider)
//  WiFi intentionally disabled; no wireless stack started.
// ============================================================
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Preferences.h>
#include <math.h>
#include "esp_system.h"
#include "esp32-hal-cpu.h"

// ==================== CONFIG ====================
#define SCREEN_W 128
#define SCREEN_H 64
#define OLED_ADDR 0x3C
#define SDA_PIN 21
#define SCL_PIN 22
#define BTN_UP 32
#define BTN_DOWN 33
#define BTN_LEFT 27
#define BTN_RIGHT 26
#define BUZZER 4
#define BATT_PIN 34

#define DEBOUNCE_MS 40
#define LONG_PRESS_MS 700

#define BI_UP 0
#define BI_DOWN 1
#define BI_LEFT 2
#define BI_RIGHT 3

#define LOG_MAX 24
#define LOG_MSG_LEN 20
#define BOOT_CLEANUP_THRESHOLD 12
#define HEAP_LOW_THRESHOLD 30000

// ==================== TYPES ====================
enum Act : uint8_t {
  A_NONE, A_UP, A_DOWN, A_LEFT, A_RIGHT,
  A_BACK, A_ALT, A_ALT_DOWN, A_ALT_RIGHT
};

struct Btn {
  bool stable;
  bool lastRead;
  unsigned long lastChange;
  unsigned long pressStart;
  bool longFired;
  bool shortPressed;
  bool longPressed;
};

struct Settings {
  uint8_t brightness;      // 0..3
  uint32_t menuTimeoutMs;  // 0 = never
  bool screenFlip;
};

struct LogEntry {
  uint32_t t;
  char msg[LOG_MSG_LEN];
};

// ==================== GLOBALS ====================
Adafruit_SSD1306 display(SCREEN_W, SCREEN_H, &Wire, -1);
Preferences prefs;
Btn btn[4];
Settings cfg;
const uint8_t BTN_PINS[4] = {BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT};
unsigned long lastActivity = 0;
uint32_t loopTimeUs = 0;
uint32_t frameCount = 0;
uint32_t minFreeHeap = 0xFFFFFFFF;
uint32_t bootCount = 0;
uint32_t cleanupCount = 0;
float batteryVoltage = 0.0f;
uint8_t batteryPercent = 0;

// Debug log (ring buffer, RAM only)
LogEntry logs[LOG_MAX];
uint8_t logHead = 0;
uint8_t logFill = 0;

// ==================== FORWARD DECLS ====================
void updateButtons();
void clearBtnFlags();
Act getAction();
inline bool isButtonDown(uint8_t i) { return btn[i].stable == LOW; }
void beep(uint16_t freq, uint16_t dur);

void clearDisp();
void drawMiniHeader(const char *t);
void drawHint(const char *m);
void drawProgressBar(int x, int y, int w, int h, uint8_t pct);

void loadCfg();
void saveCfg();
void applyBrightness();
void applyScreenFlip();
void loadBootCounter();
void autoCleanupTempMemory();
void autoFixMemory();
void logEvent(const char *msg);

void updateBattery();
float readBatteryVoltage();

int runMenu(const char *title, const char *items[], int count, int startSel);

void runShooter();
void runPong();
void runDino();
void runTetris();
void runSnake();
void runGameMenu();

void runTimer();
void runSleep();
void runAppsMenu();

void runBrightness();
void runTimeout();
void runFlip();
void runDiagnostics();
void runLogs();
void runMemoryTools();
void runReboot();
void runSettings();

void renderHome();

// ==================== PROGMEM ICONS (8x8) ====================
const uint8_t ICON_SHIP[]   PROGMEM = {0x18,0x3C,0x7E,0xFF,0x7E,0x24,0x42,0x00};
const uint8_t ICON_PONG[]   PROGMEM = {0x00,0x00,0x18,0x18,0x00,0x00,0x66,0x66};
const uint8_t ICON_DINO[]   PROGMEM = {0x0F,0x1F,0x1D,0x1F,0x1F,0x1F,0x3F,0x7F};
const uint8_t ICON_TETRIS[] PROGMEM = {0x66,0x66,0x66,0x66,0xFF,0xFF,0x00,0x00};
const uint8_t ICON_SNAKE[]  PROGMEM = {0xFF,0x81,0xBD,0xA5,0xBD,0x81,0xFF,0x00};
const uint8_t ICON_TIMER[]  PROGMEM = {0x3C,0x42,0x81,0x81,0xA5,0x81,0x42,0x3C};
const uint8_t ICON_SLEEP[]  PROGMEM = {0x18,0x3C,0x7E,0x7E,0x7E,0x3C,0x18,0x00};
const uint8_t ICON_BRIGHT[] PROGMEM = {0x18,0x18,0x24,0x18,0x18,0x24,0x18,0x18};
const uint8_t ICON_TIME[]   PROGMEM = {0x3C,0x42,0x81,0xBD,0xBD,0x81,0x42,0x3C};
const uint8_t ICON_FLIP[]   PROGMEM = {0xFF,0x81,0x81,0xFF,0xFF,0x81,0x81,0xFF};
const uint8_t ICON_DEBUG[]  PROGMEM = {0x7E,0x81,0xA5,0x81,0xBD,0x81,0x7E,0x00};
const uint8_t ICON_LOGS[]   PROGMEM = {0xFF,0x81,0xFF,0x81,0xFF,0x81,0xFF,0x81};
const uint8_t ICON_MEM[]    PROGMEM = {0x7E,0xC3,0xDB,0xDB,0xDB,0xDB,0xC3,0x7E};
const uint8_t ICON_REBOOT[] PROGMEM = {0x3C,0x42,0x81,0x80,0x80,0x81,0x42,0x3C};
const uint8_t ICON_GEAR[]   PROGMEM = {0x24,0x7E,0xFF,0x5A,0x5A,0xFF,0x7E,0x24};
const uint8_t ICON_GAMES[]  PROGMEM = {0x3C,0x66,0xDB,0xDB,0xDB,0xDB,0x66,0x3C};
const uint8_t ICON_APPS[]   PROGMEM = {0x00,0x66,0x66,0x00,0x00,0x66,0x66,0x00};
const uint8_t ICON_HOME[]   PROGMEM = {0x18,0x3C,0x7E,0xFF,0x18,0x18,0x18,0x18};
const uint8_t ICON_BATTERY[]PROGMEM = {0x3C,0x42,0x42,0x42,0x42,0x42,0x3C,0x00};

// Difficulty icons
const uint8_t ICON_EASY[]   PROGMEM = {0x00,0x00,0x18,0x3C,0x18,0x00,0x00,0x00};
const uint8_t ICON_MED[]    PROGMEM = {0x00,0x18,0x3C,0x7E,0x3C,0x18,0x00,0x00};
const uint8_t ICON_HARD[]   PROGMEM = {0x18,0x3C,0x7E,0xFF,0x7E,0x3C,0x18,0x00};

// ==================== PROGMEM WALLPAPER (Cat Night) ====================
const uint8_t wallpaper[] PROGMEM = {
  0x00,0x00,0x00,0x00,0x00,0xf8,0x15,0xc5,0x80,0x00,0x40,0x3a,0xff,0xf8,0x00,0x00,
  0x40,0x00,0x00,0x00,0x05,0x40,0x0a,0x8a,0xe0,0x80,0x80,0xb7,0xff,0xfc,0x00,0x00,
  0x01,0x40,0x00,0x00,0xaa,0xb0,0x04,0x05,0x50,0x00,0x01,0x9e,0xff,0xfe,0x00,0x80,
  0x00,0x00,0x00,0x00,0x7f,0xfe,0x00,0x0a,0xf8,0x02,0x03,0x2d,0x7f,0xff,0x00,0x00,
  0x01,0x40,0x00,0x20,0x00,0x01,0xe3,0x57,0xfc,0x02,0x01,0x4e,0x7f,0xff,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x1c,0xaf,0xe0,0x00,0x0a,0x2c,0x7e,0x7f,0x80,0x00,
  0x00,0x00,0x00,0x00,0x04,0x00,0x00,0x50,0x00,0x00,0x11,0x5e,0xfd,0x7f,0x80,0x00,
  0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x08,0x00,0x3b,0xfe,0x7f,0xc0,0x00,
  0xe0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x08,0x00,0x77,0xfd,0xff,0xc0,0x02,
  0x50,0x00,0x08,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x08,0x7b,0xff,0xff,0xc0,0x00,
  0x38,0x00,0x00,0x00,0x00,0x00,0x10,0x00,0x00,0x00,0x10,0x57,0x5f,0xff,0xe0,0x00,
  0x58,0x00,0x00,0x0c,0x01,0x80,0x00,0x00,0x00,0x00,0x08,0x7e,0x9f,0xff,0xe0,0x00,
  0x39,0x80,0x00,0x0a,0x01,0x40,0x40,0x00,0x00,0x00,0x11,0x3f,0x5f,0xff,0xe0,0x00,
  0x57,0x40,0x00,0x12,0x02,0x40,0x00,0x00,0x00,0x00,0x02,0x5e,0x9f,0xff,0xe0,0x00,
  0xaa,0xe0,0x00,0x12,0x02,0x20,0x00,0x00,0x00,0x00,0x22,0x1f,0x3f,0xdf,0xe0,0x00,
  0x41,0x70,0x00,0x21,0x04,0x20,0x00,0x00,0x00,0x00,0x42,0x5e,0xff,0xaf,0xe0,0x40,
  0x04,0xbe,0x00,0x21,0x08,0x20,0x00,0x02,0x00,0x00,0x21,0x1b,0xff,0xcf,0xe0,0x00,
  0x02,0x6f,0x80,0x43,0xf0,0x20,0x00,0x00,0x00,0x00,0x00,0x3d,0xff,0xaf,0xe0,0x00,
  0x09,0x55,0xc0,0x4c,0x20,0x30,0x00,0x00,0x00,0x00,0x00,0x7a,0xff,0x5f,0xc0,0x00,
  0x04,0x2b,0xe0,0x80,0x40,0x20,0x00,0x10,0x00,0x08,0x24,0xdd,0xff,0xbf,0xc4,0x00,
  0x00,0x95,0x71,0x00,0x00,0x30,0x00,0x00,0x20,0x08,0x04,0xbb,0xff,0xff,0xc0,0x00,
  0x85,0x6f,0xe1,0x00,0x00,0x20,0x00,0x00,0x00,0x04,0x02,0x6f,0xfb,0xff,0x80,0x00,
  0x4a,0x80,0x02,0x00,0x00,0x10,0x00,0x00,0x00,0x04,0x00,0x17,0x79,0xff,0x80,0x01,
  0x94,0x00,0x02,0x00,0x00,0x08,0x00,0x00,0x00,0x00,0x00,0x4b,0xf5,0xff,0x00,0x01,
  0x28,0x00,0x02,0x00,0x00,0x08,0x00,0x00,0x00,0x02,0x04,0xa7,0x69,0xff,0x04,0x03,
  0x00,0x04,0x02,0x00,0x00,0x04,0x00,0x00,0x00,0x00,0x02,0x42,0xf3,0xfe,0x00,0x0f,
  0x00,0x00,0x04,0x00,0x00,0x04,0x00,0x00,0x00,0x00,0x00,0x27,0xbf,0xfc,0x00,0x1f,
  0x00,0x00,0x04,0x00,0x00,0x04,0x00,0x04,0x00,0x40,0x01,0x03,0xff,0xf8,0x00,0x7e,
  0x00,0x00,0x02,0x00,0x00,0x07,0x80,0x00,0x00,0x00,0x00,0x01,0x5f,0xf0,0x0f,0xfd,
  0x00,0x01,0x01,0x00,0x00,0x08,0x00,0x00,0x80,0x00,0x10,0x08,0x2a,0xe0,0x3f,0xea,
  0x10,0x00,0x02,0x80,0x00,0x16,0x00,0x00,0x00,0x00,0x04,0x01,0x1d,0xc0,0xff,0x5c,
  0x00,0x00,0x00,0x4a,0x80,0x31,0x80,0x00,0x00,0x00,0x03,0x00,0x8f,0x03,0xfa,0xa9,
  0x00,0x00,0x00,0x55,0x00,0xc8,0x00,0x00,0x00,0x00,0x00,0x60,0x1c,0x07,0xbd,0x40,
  0x00,0x00,0x00,0x40,0x01,0x04,0x00,0x00,0x00,0x00,0x00,0x1b,0xa0,0x1f,0x56,0x95,
  0x00,0x00,0x00,0x80,0x01,0x00,0x00,0x00,0x00,0x00,0x0e,0x00,0x00,0x7e,0x9d,0x2a,
  0x00,0x00,0x00,0x80,0x02,0x00,0x10,0x00,0x00,0x03,0xd5,0xc0,0x3f,0xfc,0x2a,0x50,
  0x00,0x00,0x00,0x80,0x02,0x00,0x00,0x00,0x00,0x05,0x6a,0xbf,0xd2,0x94,0x54,0x00,
  0x00,0x00,0x01,0x00,0x02,0x00,0x00,0x00,0x00,0x1a,0x05,0x5e,0xa5,0x28,0x20,0x00,
  0x00,0x00,0x01,0x00,0x02,0x00,0x00,0x00,0x00,0x3d,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x02,0x20,0x01,0x00,0x00,0x00,0x00,0x00,0x52,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x02,0x40,0x01,0x00,0x00,0x00,0x00,0x00,0x01,0x50,0x00,0x08,0x04,0x42,
  0x00,0x00,0x04,0x20,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x02,0xa2,0x10,
  0x00,0x00,0x04,0x40,0x00,0x80,0x00,0x04,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x02,0x10,0x08,0x80,0x00,0x40,0x40,0x0e,0x00,0x00,0x40,0x00,0x00,0x00,0x00,0x00,
  0x22,0x08,0x10,0x00,0x00,0x40,0x40,0x57,0x00,0x00,0x58,0x02,0x00,0x00,0x00,0x00,
  0xab,0x4c,0x20,0x00,0x00,0x49,0xd1,0x4b,0x48,0x94,0xdc,0x92,0x90,0x00,0xa1,0x10,
  0xf7,0x2a,0x40,0x00,0x00,0x27,0xea,0x7f,0xe5,0x7a,0xff,0xef,0xc7,0xfe,0x5c,0xef,
  0x80,0x90,0x80,0x00,0x00,0x40,0x11,0x00,0x0a,0x80,0x00,0x00,0x00,0x01,0x22,0x00,
  0x00,0x00,0x80,0x04,0x00,0x20,0x80,0x80,0x10,0x40,0x3f,0xe5,0xf1,0xbc,0x00,0x00,
  0x24,0x41,0x01,0x00,0x00,0x42,0x00,0x00,0x48,0x14,0x00,0x00,0xa8,0x08,0x00,0x11,
  0x10,0x01,0x02,0x02,0x00,0x20,0x00,0x00,0x00,0x08,0x03,0xf0,0x5f,0xa0,0x88,0x00,
  0x02,0x01,0x04,0x00,0x00,0x40,0x00,0x00,0x00,0x40,0x04,0x25,0x00,0x01,0x10,0x00,
  0x44,0x02,0x02,0x04,0x00,0x80,0x00,0x0a,0x00,0x11,0x11,0x4a,0x10,0x04,0x44,0x48,
  0x80,0x02,0x04,0x02,0x00,0x80,0x00,0x35,0x00,0x04,0x40,0x00,0x04,0x00,0x11,0x10,
  0x20,0x02,0x02,0x04,0x00,0x80,0x03,0xc2,0x00,0x00,0x00,0x00,0x00,0x40,0x00,0x40,
  0x00,0x02,0x04,0x03,0x00,0x40,0x3c,0x05,0x00,0x00,0x00,0x00,0x00,0x00,0x02,0x04,
  0x00,0x02,0x04,0x00,0xfc,0x5f,0xc0,0x02,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x08,
  0x00,0x01,0x04,0x00,0x03,0xe0,0x00,0x04,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x01,0x02,0x00,0x00,0x00,0x00,0x08,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0xfd,0x00,0x00,0x00,0x00,0xf0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0xe0,0x00,0x00,0x0f,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x1f,0xf0,0x1f,0xf0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x0f,0xe0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
};

// ==================== DEBUG LOG ====================
void logEvent(const char *msg) {
  logs[logHead].t = millis() / 1000;
  strncpy(logs[logHead].msg, msg, LOG_MSG_LEN - 1);
  logs[logHead].msg[LOG_MSG_LEN - 1] = 0;
  logHead = (logHead + 1) % LOG_MAX;
  if (logFill < LOG_MAX) logFill++;
}

// ==================== BUTTON ENGINE ====================
void updateButtons() {
  for (int i = 0; i < 4; i++) {
    bool r = digitalRead(BTN_PINS[i]);
    if (r != btn[i].lastRead) btn[i].lastChange = millis();
    if ((millis() - btn[i].lastChange) > DEBOUNCE_MS && r != btn[i].stable) {
      btn[i].stable = r;
      if (r == LOW) {
        btn[i].pressStart = millis();
        btn[i].longFired = false;
      } else if (!btn[i].longFired) {
        btn[i].shortPressed = true;
      }
    }
    if (btn[i].stable == LOW && !btn[i].longFired &&
        (millis() - btn[i].pressStart) >= LONG_PRESS_MS) {
      btn[i].longFired = true;
      btn[i].longPressed = true;
    }
    btn[i].lastRead = r;
  }
}

void clearBtnFlags() {
  for (int i = 0; i < 4; i++) {
    btn[i].shortPressed = false;
    btn[i].longPressed = false;
  }
}

Act getAction() {
  updateButtons();
  Act r = A_NONE;
  if (btn[BI_LEFT].longPressed)       { btn[BI_LEFT].longPressed = false;  r = A_BACK; }
  else if (btn[BI_RIGHT].longPressed) { btn[BI_RIGHT].longPressed = false; r = A_ALT_RIGHT; }
  else if (btn[BI_DOWN].longPressed)  { btn[BI_DOWN].longPressed = false;  r = A_ALT_DOWN; }
  else if (btn[BI_UP].longPressed)    { btn[BI_UP].longPressed = false;    r = A_ALT; }
  else if (btn[BI_UP].shortPressed)   { btn[BI_UP].shortPressed = false;   r = A_UP; }
  else if (btn[BI_DOWN].shortPressed) { btn[BI_DOWN].shortPressed = false; r = A_DOWN; }
  else if (btn[BI_LEFT].shortPressed) { btn[BI_LEFT].shortPressed = false; r = A_LEFT; }
  else if (btn[BI_RIGHT].shortPressed){ btn[BI_RIGHT].shortPressed = false;r = A_RIGHT; }
  if (r != A_NONE) lastActivity = millis();
  return r;
}

void beep(uint16_t freq, uint16_t dur) { tone(BUZZER, freq, dur); }

// ==================== DISPLAY HELPERS ====================
void clearDisp() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setTextWrap(false);
}

void drawMiniHeader(const char *t) {
  display.setTextSize(1);
  display.setCursor(2, 1);
  display.print(t);
  display.drawFastHLine(0, 10, 128, SSD1306_WHITE);
}

void drawHint(const char *m) {
  display.setTextSize(1);
  display.setCursor(2, 57);
  display.print(m);
}

void drawProgressBar(int x, int y, int w, int h, uint8_t pct) {
  display.drawRect(x, y, w, h, SSD1306_WHITE);
  int fill = (w - 2) * pct / 100;
  if (fill > 0) display.fillRect(x + 1, y + 1, fill, h - 2, SSD1306_WHITE);
}

// ==================== STORAGE / BOOT ====================
void loadCfg() {
  prefs.begin("neon", true);
  cfg.brightness    = prefs.getUChar("br", 2);
  cfg.menuTimeoutMs = prefs.getULong("to", 20000);
  cfg.screenFlip    = prefs.getBool("flip", false);
  prefs.end();
}

void saveCfg() {
  prefs.begin("neon", false);
  prefs.putUChar("br", cfg.brightness);
  prefs.putULong("to", cfg.menuTimeoutMs);
  prefs.putBool("flip", cfg.screenFlip);
  prefs.end();
}

void applyBrightness() {
  static const uint8_t c[4] = {20, 90, 170, 255};
  display.ssd1306_command(SSD1306_SETCONTRAST);
  display.ssd1306_command(c[cfg.brightness & 3]);
}

void applyScreenFlip() { display.setRotation(cfg.screenFlip ? 2 : 0); }

void loadBootCounter() {
  prefs.begin("sys", false);
  bootCount = prefs.getUInt("boots", 0) + 1;
  cleanupCount = prefs.getUInt("cln", 0);
  prefs.putUInt("boots", bootCount);
  prefs.end();
  char b[LOG_MSG_LEN];
  snprintf(b, sizeof(b), "BOOT #%lu", (unsigned long)bootCount);
  logEvent(b);
}

void autoCleanupTempMemory() {
  // Every BOOT_CLEANUP_THRESHOLD boots, wipe the "tmp" namespace
  if (bootCount > 0 && (bootCount % BOOT_CLEANUP_THRESHOLD) == 0) {
    prefs.begin("tmp", false);
    prefs.clear();
    prefs.end();
    cleanupCount++;
    prefs.begin("sys", false);
    prefs.putUInt("cln", cleanupCount);
    prefs.end();
    logEvent("AUTO CLEAN");
  }
}

void autoFixMemory() {
  uint32_t fh = ESP.getFreeHeap();
  if (fh < minFreeHeap) minFreeHeap = fh;
  if (fh < HEAP_LOW_THRESHOLD) {
    // Clear log buffer (RAM only) and reset log pointers
    logHead = 0;
    logFill = 0;
    logEvent("MEM FIX");
  }
}

// ==================== BATTERY ====================
float readBatteryVoltage() {
  int raw = analogRead(BATT_PIN);
  // Assume 2:1 divider, 3.3V ADC ref, 12-bit
  return raw * 3.3f / 4095.0f * 2.0f;
}

void updateBattery() {
  batteryVoltage = readBatteryVoltage();
  // Li-Ion 3.0V=0%, 4.2V=100%
  float pct = (batteryVoltage - 3.0f) / 1.2f * 100.0f;
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  batteryPercent = (uint8_t)pct;
}

// ==================== GENERIC LIST MENU ====================
int runMenu(const char *title, const char *items[], int count, int startSel) {
  clearBtnFlags();
  int sel = startSel;
  while (true) {
    clearDisp();
    drawMiniHeader(title);
    int visible = 5;
    int start = sel - visible / 2;
    if (start < 0) start = 0;
    if (start > count - visible) start = count - visible;
    if (start < 0) start = 0;
    for (int i = 0; i < visible && (start + i) < count; i++) {
      int idx = start + i, y = 13 + i * 10;
      bool selected = (idx == sel);
      if (selected) {
        display.fillRect(0, y - 1, 122, 10, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK);
      }
      display.setCursor(4, y);
      display.print(selected ? "> " : "  ");
      display.print(items[idx]);
      display.setTextColor(SSD1306_WHITE);
    }
    display.display();
    Act a = getAction();
    if (a == A_UP)    sel = (sel - 1 + count) % count;
    if (a == A_DOWN)  sel = (sel + 1) % count;
    if (a == A_RIGHT) { clearBtnFlags(); return sel; }
    if (a == A_BACK)  { clearBtnFlags(); return -1; }
    delay(20);
  }
}

// ==================== SPACE SHOOTER (FIXED) ====================
void runShooter() {
  static const uint8_t ship[] PROGMEM = {
    0b00011000, 0b00111100, 0b01111110, 0b11111111,
    0b01111110, 0b00100100, 0b01000010
  };
  struct Star   { int8_t x, y, sp; };
  struct Bullet { int8_t x, y; bool active; };
  struct Enemy  { int8_t x, y; uint8_t sz; bool alive; };
  Star stars[10];
  Bullet bullets[3];
  Enemy enemies[3];

  int8_t playerX = 64;
  const uint8_t playerY = 48;
  int score = 0, health = 10;
  bool gameOver = false;
  int enemySpeed = 1;
  unsigned long lastFire = 0, lastFrame = 0;
  bool prevFireDown = false;

  for (int i = 0; i < 10; i++) { stars[i].x = random(0,128); stars[i].y = random(0,64); stars[i].sp = random(1,3); }
  for (int i = 0; i < 3; i++)  { enemies[i].x = random(0,120); enemies[i].y = random(-60,-10); enemies[i].sz = random(6,12); enemies[i].alive = true; }
  for (int i = 0; i < 3; i++)  bullets[i].active = false;

  logEvent("SHOOTER ON");
  clearBtnFlags();

  while (true) {
    Act a = getAction();
    if (a == A_BACK) { clearBtnFlags(); return; }
    if (millis() - lastFrame < 33) { delay(2); continue; }
    lastFrame = millis();

    if (isButtonDown(BI_LEFT)  && playerX > 6)   playerX -= 3;
    if (isButtonDown(BI_RIGHT) && playerX < 122) playerX += 3;

    // FIX: fire on rising edge only, no conflict with UP navigation
    bool fireDown = isButtonDown(BI_UP);
    if (fireDown && !prevFireDown && millis() - lastFire > 180) {
      for (int i = 0; i < 3; i++) {
        if (!bullets[i].active) {
          bullets[i].x = playerX;
          bullets[i].y = playerY - 2;  // FIX: spawn from nose, not inside sprite
          bullets[i].active = true;
          beep(1200, 20);
          lastFire = millis();
          break;
        }
      }
    }
    prevFireDown = fireDown;

    for (int i = 0; i < 10; i++) {
      stars[i].y += stars[i].sp;
      if (stars[i].y >= 64) { stars[i].y = 0; stars[i].x = random(0,128); }
    }
    for (int i = 0; i < 3; i++) {
      if (bullets[i].active) { bullets[i].y -= 6; if (bullets[i].y < 0) bullets[i].active = false; }
    }
    for (int i = 0; i < 3; i++) {
      if (!enemies[i].alive) continue;
      enemies[i].y += enemySpeed;
      if (enemies[i].y > 64) {
        if (health > 0) health--;  // FIX: clamp, no negative
        enemies[i].x = random(0,120); enemies[i].y = random(-60,-10);
        enemies[i].sz = random(6,12); beep(300, 60);
      }
      // FIX: full bounding-box player vs enemy
      if (enemies[i].y + enemies[i].sz >= playerY &&
          enemies[i].y <= playerY + 7 &&
          enemies[i].x + enemies[i].sz >= playerX - 4 &&
          enemies[i].x <= playerX + 4) gameOver = true;
    }
    // FIX: bullet vs enemy accounts for bullet's 4-pixel height
    for (int i = 0; i < 3; i++) {
      if (!bullets[i].active) continue;
      for (int j = 0; j < 3; j++) {
        if (!enemies[j].alive) continue;
        int s = enemies[j].sz;
        bool hit = (bullets[i].x >= enemies[j].x && bullets[i].x <= enemies[j].x + s) &&
                   (bullets[i].y + 4 >= enemies[j].y && bullets[i].y <= enemies[j].y + s);
        if (hit) {
          bullets[i].active = false; score += 5;
          enemies[j].x = random(0,120); enemies[j].y = random(-60,-10);
          enemies[j].sz = random(6,12);
          beep(800, 20);
          if (score % 25 == 0 && enemySpeed < 5) enemySpeed++;
        }
      }
    }

    if (gameOver || health <= 0) {
      beep(200, 300);
      clearDisp();
      display.setTextSize(2); display.setCursor(12, 15); display.print(F("GAME OVER"));
      display.setTextSize(1); display.setCursor(35, 40); display.print(F("Score: ")); display.print(score);
      display.display();
      delay(1500);
      while (isButtonDown(BI_UP) || isButtonDown(BI_DOWN) || isButtonDown(BI_LEFT) || isButtonDown(BI_RIGHT)) delay(10);
      clearBtnFlags(); return;
    }

    clearDisp();
    for (int i = 0; i < 10; i++) display.drawPixel(stars[i].x, stars[i].y, SSD1306_WHITE);
    for (int i = 0; i < 3; i++) if (bullets[i].active) display.drawFastVLine(bullets[i].x, bullets[i].y, 4, SSD1306_WHITE);
    for (int i = 0; i < 3; i++) {
      if (!enemies[i].alive) continue;
      int x = enemies[i].x, y = enemies[i].y, s = enemies[i].sz;
      int cx = x + s/2, cy = y + s/2;
      display.drawCircle(cx, cy, s/2, SSD1306_WHITE);
      display.drawPixel(cx - 1, cy - 1, SSD1306_WHITE);
      display.drawPixel(cx + 1, cy - 1, SSD1306_WHITE);
    }
    display.drawBitmap(playerX - 4, playerY, ship, 8, 7, SSD1306_WHITE);
    display.setTextSize(1); display.setCursor(0, 0); display.print(F("S:")); display.print(score);
    display.setCursor(90, 0); display.print(F("HP:"));
    int hw = (health * 18) / 10;
    if (hw < 0) hw = 0; if (hw > 18) hw = 18;   // FIX: clamp
    display.drawRect(108, 0, 20, 7, SSD1306_WHITE);
    display.fillRect(109, 1, hw, 5, SSD1306_WHITE);
    display.display();
  }
}

// ==================== PONG (WITH DIFFICULTY) ====================
void runPong() {
  // Difficulty selection
  const char *diffs[] = {"Easy", "Medium", "Hard"};
  const uint8_t *diffIcons[] = {ICON_EASY, ICON_MED, ICON_HARD};
  clearBtnFlags();
  int diff = 1;
  bool chosen = false;
  while (!chosen) {
    clearDisp();
    drawMiniHeader("PONG");
    for (int i = 0; i < 3; i++) {
      int y = 22 + i * 13;
      bool sel = (i == diff);
      if (sel) {
        display.fillRect(2, y - 1, 124, 12, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK);
      }
      display.drawBitmap(6, y + 1, diffIcons[i], 8, 8, SSD1306_WHITE);
      display.setCursor(20, y + 1);
      display.print(diffs[i]);
      display.setTextColor(SSD1306_WHITE);
    }
    display.display();
    Act a = getAction();
    if (a == A_UP)    diff = (diff - 1 + 3) % 3;
    if (a == A_DOWN)  diff = (diff + 1) % 3;
    if (a == A_RIGHT) { chosen = true; }
    if (a == A_BACK)  { clearBtnFlags(); return; }
    delay(30);
  }
  // Easy: cpuSpeed 1, err 8 | Med: 2, err 4 | Hard: 3, err 1
  const int8_t cpuSpeed[3] = {1, 2, 3};
  const int8_t cpuErr[3]   = {8, 4, 1};

  const uint8_t PH = 20, PW = 3;
  const uint8_t PX = 120, CX = 5;
  int8_t bx, by, bdx, bdy, cy, py;
  int pScore = 0, cScore = 0;
  unsigned long bUpd, pUpd;
  bool ballJustReset = false;

  auto resetBall = [&](int dir) { bx = 64; by = 32; bdx = dir; bdy = (random(0,2) == 0) ? 1 : -1; ballJustReset = true; };
  auto drawPaddle = [&](uint8_t x, int8_t y) { display.fillRect(x, y, PW, PH, SSD1306_WHITE); };
  auto drawScore = [&]() {
    display.setTextSize(1); display.setCursor(42, 2); display.print(cScore);
    display.setCursor(78, 2); display.print(pScore);
  };

  cy = (64 - PH) / 2; py = (64 - PH) / 2;
  resetBall(1);
  bUpd = millis(); pUpd = millis();

  clearDisp();
  for (int y = 2; y < 62; y += 4) display.drawPixel(64, y, SSD1306_WHITE);
  display.drawRect(0, 0, 128, 64, SSD1306_WHITE);
  drawScore();
  display.display();
  delay(500);
  logEvent("PONG ON");
  clearBtnFlags();

  while (true) {
    Act a = getAction();
    if (a == A_BACK) { clearBtnFlags(); return; }
    unsigned long now = millis();
    bool update = false;

    if (now > bUpd) {
      bUpd += 16;
      int8_t nx = bx + bdx, ny = by + bdy;
      if (ny <= 1 || ny >= 62) { bdy = -bdy; ny += bdy + bdy; beep(600, 15); }
      // FIX: score only once per crossing
      if (!ballJustReset && nx <= 1)  { pScore++; beep(1000, 80); resetBall(1);  nx = bx; ny = by; }
      if (!ballJustReset && nx >= 126){ cScore++; beep(500, 80);  resetBall(-1); nx = bx; ny = by; }
      if (ballJustReset) ballJustReset = false;
      if (nx <= CX + PW && nx >= CX && ny >= cy && ny <= cy + PH) { bdx = -bdx; nx += bdx + bdx; beep(800, 15); }
      if (nx >= PX && nx <= PX + PW && ny >= py && ny <= py + PH) { bdx = -bdx; nx += bdx + bdx; beep(900, 15); }
      display.drawPixel(bx, by, BLACK);
      if (bx + 1 < 128) display.drawPixel(bx + 1, by, BLACK);
      bx = nx; by = ny;
      display.drawPixel(bx, by, SSD1306_WHITE);
      if (bx + 1 < 128) display.drawPixel(bx + 1, by, SSD1306_WHITE);
      update = true;
    }

    if (now > pUpd) {
      pUpd += 33;
      display.fillRect(CX, 1, PW, 62, BLACK);
      int cc = cy + PH / 2;
      // FIX: difficulty-scaled AI with error
      int target = by + (int8_t)(random(-cpuErr[diff], cpuErr[diff] + 1));
      if (cc < target - 2) cy += cpuSpeed[diff];
      if (cc > target + 2) cy -= cpuSpeed[diff];
      if (cy < 1) cy = 1;
      if (cy + PH > 63) cy = 63 - PH;
      drawPaddle(CX, cy);
      display.fillRect(PX, 1, PW, 62, BLACK);
      if (isButtonDown(BI_UP))   py -= 2;
      if (isButtonDown(BI_DOWN)) py += 2;
      if (py < 1) py = 1;
      if (py + PH > 63) py = 63 - PH;
      drawPaddle(PX, py);
      update = true;
    }

    if (update) {
      drawScore();
      if (pScore >= 7 || cScore >= 7) {
        clearDisp();
        display.setTextSize(2); display.setCursor(10, 15);
        display.print(pScore >= 7 ? F("YOU WIN!") : F("CPU WINS"));
        display.setTextSize(1); display.setCursor(35, 45);
        display.print(pScore); display.print(F(" - ")); display.print(cScore);
        display.display();
        beep(pScore >= 7 ? 1500 : 300, 400);
        delay(2500);
        while (isButtonDown(BI_UP) || isButtonDown(BI_DOWN) || isButtonDown(BI_LEFT) || isButtonDown(BI_RIGHT)) delay(10);
        clearBtnFlags(); return;
      }
      display.display();
    }
    delay(1);
  }
}

// ==================== DINO RUNNER ====================
static const uint8_t PROGMEM dinoRun1[] = {
  0x00,0x0F,0x80,0x00,0x1F,0xC0,0x00,0x1D,0xC0,0x00,0x1F,0xC0,
  0x00,0x1F,0xC0,0x00,0x1F,0x00,0x80,0x1F,0x80,0x80,0x3F,0xC0,
  0xC0,0x3F,0xC0,0xE0,0x7F,0xC0,0xF0,0xFF,0xC0,0xFF,0xFF,0xC0,
  0x7F,0xFF,0xC0,0x3F,0xFF,0xC0,0x1F,0xFF,0x80,0x0F,0xFF,0x00,
  0x07,0xFE,0x00,0x07,0x1C,0x00,0x06,0x18,0x00,0x07,0x00,0x00};
static const uint8_t PROGMEM dinoRun2[] = {
  0x00,0x0F,0x80,0x00,0x1F,0xC0,0x00,0x1D,0xC0,0x00,0x1F,0xC0,
  0x00,0x1F,0xC0,0x00,0x1F,0x00,0x80,0x1F,0x80,0x80,0x3F,0xC0,
  0xC0,0x3F,0xC0,0xE0,0x7F,0xC0,0xF0,0xFF,0xC0,0xFF,0xFF,0xC0,
  0x7F,0xFF,0xC0,0x3F,0xFF,0xC0,0x1F,0xFF,0x80,0x0F,0xFF,0x00,
  0x07,0xFE,0x00,0x07,0x1C,0x00,0x06,0x18,0x00,0x00,0x1C,0x00};
static const uint8_t PROGMEM dinoDuck[] = {
  0x00,0x00,0x70,0x00,0x00,0xF0,0x00,0x00,0xB0,0x00,0x00,0xF0,
  0x80,0x00,0xF0,0xC0,0x01,0xF0,0xE2,0xFF,0xF0,0xFF,0xFF,0xFF,
  0x7F,0xFF,0xFF,0x3F,0xFF,0xFF,0x0F,0xFF,0x00,0x06,0x18,0x00};
static const uint8_t PROGMEM cactusSmall[] = {
  0x30,0x30,0x30,0x58,0x5A,0x5A,0x7A,0x3C,0x30,0x30,0x30,0x30,0x30,0x30};
static const uint8_t PROGMEM cactusBig[] = {
  0x0E,0x00,0x0E,0x00,0x0E,0x00,0x4E,0x00,0x4E,0x40,0x4E,0x40,
  0x4E,0x40,0x4E,0x40,0x7C,0x40,0x3C,0x40,0x0F,0xC0,0x0E,0x00,
  0x0E,0x00,0x0E,0x00,0x0E,0x00,0x0E,0x00,0x0E,0x00,0x0E,0x00,
  0x0E,0x00,0x0E,0x00};
static const uint8_t PROGMEM pteroUp[] = {
  0x00,0x00,0x18,0x18,0x3C,0x3C,0x7E,0x7E,0x3F,0xF0,0x1F,0xE0,0x0F,0x80,0x06,0x00,0x06,0x00,0x00,0x00};
static const uint8_t PROGMEM pteroDn[] = {
  0x00,0x00,0x00,0x00,0x06,0x00,0x0F,0x00,0x1F,0x80,0x3F,0xF0,0x7E,0x7E,0x3C,0x3C,0x18,0x18,0x00,0x00};

#define DINO_GROUND 56
#define DINO_TREX_X 12
#define DINO_JUMP_V -10
#define DINO_GRAV_UP 1
#define DINO_GRAV_DOWN 2
#define DINO_JUMP_CUT -3
#define DINO_AUTO_EXIT_MS 4000

void dinoDrawSprite(int16_t x, int16_t y, const uint8_t *data, uint8_t w, uint8_t h) {
  int stride = (w + 7) / 8;
  for (uint8_t row = 0; row < h; row++)
    for (uint8_t b = 0; b < stride; b++) {
      uint8_t byte = pgm_read_byte(data + row * stride + b);
      for (uint8_t k = 0; k < 8; k++)
        if (byte & (0x80 >> k)) {
          int px = x + b*8 + k, py = y + row;
          if (px >= 0 && px < 128 && py >= 0 && py < 64) display.drawPixel(px, py, SSD1306_WHITE);
        }
    }
}

void runDino() {
  struct Cactus { bool active; bool big; int16_t x; };
  struct Ptero  { bool active; int16_t x, y; uint8_t frame, tick; };
  Cactus cacti[3];
  Ptero  pteros[2];
  int16_t trexFeet = DINO_GROUND;
  int8_t trexVy = 0;
  bool grounded = true, ducking = false, dead = false;
  uint8_t animFrm = 0, animTck = 0, blink = 0, deathTimer = 0;
  uint16_t groundOff = 0;
  int16_t nextSpawn = 130;
  uint8_t scrollSpeed = 2;
  bool jumpPrev = false, deadHandled = false;
  uint16_t hiScore = 0, score = 0;
  uint8_t lives = 3;

  prefs.begin("trex", true);
  hiScore = prefs.getUShort("hi", 0);
  prefs.end();
  if (hiScore == 0xFFFF) hiScore = 0;

  for (int i = 0; i < 3; i++) cacti[i].active = false;
  for (int i = 0; i < 2; i++) pteros[i].active = false;
  logEvent("DINO ON");
  clearBtnFlags();
  unsigned long lastFrame = millis();

  while (true) {
    if (millis() - lastFrame < 33) { delay(2); continue; }
    lastFrame = millis();
    updateButtons();
    if (btn[BI_LEFT].longFired) { btn[BI_LEFT].longFired = false; clearBtnFlags(); return; }

    if (dead) {
      deathTimer++;
      if (!deadHandled && deathTimer > 30 && score > hiScore) {
        hiScore = score;
        prefs.begin("trex", false); prefs.putUShort("hi", hiScore); prefs.end();
      }
      clearDisp();
      display.setCursor(0, 0); display.print(F("HI:")); display.print(hiScore);
      display.print(F("  ")); display.print(score);
      display.drawFastHLine(0, DINO_GROUND, 128, SSD1306_WHITE);
      dinoDrawSprite(DINO_TREX_X, trexFeet - 20, (animFrm & 1) ? dinoRun2 : dinoRun1, 18, 20);
      if (deathTimer > 20) {
        display.fillRect(14, 20, 100, 24, SSD1306_BLACK);
        display.drawRect(14, 20, 100, 24, SSD1306_WHITE);
        display.setCursor(28, 24); display.print(F("GAME OVER"));
        display.setCursor(20, 34); display.print(F("Score: ")); display.print(score);
      }
      display.display();
      if (deathTimer > 60 && !deadHandled) {
        deadHandled = true;
        unsigned long ws = millis(); bool restarted = false;
        while (true) {
          unsigned long el = millis() - ws;
          if (el >= DINO_AUTO_EXIT_MS) break;
          updateButtons();
          if (btn[BI_LEFT].longFired) { btn[BI_LEFT].longFired = false; clearBtnFlags(); return; }
          Act a = getAction();
          if (a == A_UP || a == A_RIGHT) { restarted = true; break; }
          if (a == A_BACK) { clearBtnFlags(); return; }
          delay(30);
        }
        if (restarted) {
          trexFeet = DINO_GROUND; trexVy = 0; grounded = true; ducking = false;
          dead = false; blink = 0; deathTimer = 0;
          score = 0; lives = 3; scrollSpeed = 2; groundOff = 0; nextSpawn = 130;
          jumpPrev = false; deadHandled = false;
          for (int i = 0; i < 3; i++) cacti[i].active = false;
          for (int i = 0; i < 2; i++) pteros[i].active = false;
          continue;
        }
        clearBtnFlags(); return;
      }
      continue;
    }

    scrollSpeed = 2 + score / 300;
    if (scrollSpeed > 6) scrollSpeed = 6;
    groundOff = (groundOff + scrollSpeed) & 127;

    bool jumpHeld = isButtonDown(BI_UP);
    bool jumpEdge = jumpHeld && !jumpPrev;
    jumpPrev = jumpHeld;
    bool wantDuck = isButtonDown(BI_DOWN);

    if (grounded) {
      if (jumpEdge) { trexVy = DINO_JUMP_V; grounded = false; ducking = false; }
      else ducking = wantDuck;
    } else if (trexVy < 0 && !jumpHeld && trexVy < DINO_JUMP_CUT) trexVy = DINO_JUMP_CUT;

    if (!grounded) {
      trexFeet += trexVy;
      trexVy += (trexVy < 0) ? DINO_GRAV_UP : DINO_GRAV_DOWN;
      if (trexFeet >= DINO_GROUND) { trexFeet = DINO_GROUND; trexVy = 0; grounded = true; }
    }
    if (++animTck >= 4) { animTck = 0; animFrm ^= 1; }
    if (blink) blink--;

    nextSpawn -= scrollSpeed;
    if (nextSpawn <= 0) {
      if (score > 400 && random(0,100) < 28 && !pteros[0].active && !pteros[1].active) {
        for (int i = 0; i < 2; i++) if (!pteros[i].active) {
          pteros[i].active = true; pteros[i].x = 132;
          pteros[i].y = random(0,2) ? 30 : 46;
          pteros[i].frame = 0; pteros[i].tick = 0; break;
        }
        nextSpawn = random(150, 300);
      } else {
        for (int i = 0; i < 3; i++) if (!cacti[i].active) {
          cacti[i].active = true; cacti[i].big = random(0,2); cacti[i].x = 132; break;
        }
        nextSpawn = random(110, 260);
      }
    }
    for (int i = 0; i < 3; i++) if (cacti[i].active) {
      cacti[i].x -= scrollSpeed;
      if (cacti[i].x < -14) cacti[i].active = false;
    }
    for (int i = 0; i < 2; i++) if (pteros[i].active) {
      pteros[i].x -= scrollSpeed + 2;
      if (++pteros[i].tick >= 8) { pteros[i].tick = 0; pteros[i].frame ^= 1; }
      if (pteros[i].x < -16) pteros[i].active = false;
    }

    if (blink == 0) {
      int hx, hy, hw, hh;
      if (ducking && grounded) { hx = DINO_TREX_X + 2; hy = trexFeet - 8; hw = 16; hh = 8; }
      else { hx = DINO_TREX_X + 3; hy = trexFeet - 18; hw = 12; hh = 16; }
      bool hit = false;
      for (int i = 0; i < 3 && !hit; i++) if (cacti[i].active) {
        int w = cacti[i].big ? 12 : 8, h = cacti[i].big ? 20 : 14;
        int bx = cacti[i].x + 2, by = DINO_GROUND - h + 2;
        if (hx < bx + w - 4 && hx + hw > bx && hy < by + h - 4 && hy + hh > by) hit = true;
      }
      for (int i = 0; i < 2 && !hit; i++) if (pteros[i].active) {
        int bx = pteros[i].x + 2, by = pteros[i].y + 2;
        if (hx < bx + 12 && hx + hw > bx && hy < by + 6 && hy + hh > by) hit = true;
      }
      if (hit) {
        if (lives > 0) { lives--; blink = 90; }
        else { dead = true; deathTimer = 0; }
      }
    }
    if (score < 99999) score++;

    clearDisp();
    display.setCursor(0, 0); display.print(F("HI:")); display.print(hiScore);
    display.print(F("  ")); display.print(score);
    for (int i = 0; i < lives; i++) display.fillCircle(100 + i*8, 4, 2, SSD1306_WHITE);
    display.drawFastHLine(0, DINO_GROUND, 128, SSD1306_WHITE);
    for (int x = 0; x < 128; x += 2)
      if ((x + groundOff) & 2) display.drawPixel(x, DINO_GROUND + 1, SSD1306_WHITE);
    for (int i = 0; i < 3; i++) if (cacti[i].active) {
      if (cacti[i].big) dinoDrawSprite(cacti[i].x, DINO_GROUND - 20, cactusBig, 12, 20);
      else dinoDrawSprite(cacti[i].x, DINO_GROUND - 14, cactusSmall, 8, 14);
    }
    for (int i = 0; i < 2; i++) if (pteros[i].active) {
      const uint8_t *spr = (pteros[i].frame & 1) ? pteroDn : pteroUp;
      dinoDrawSprite(pteros[i].x, pteros[i].y, spr, 16, 10);
    }
    if (!(blink && (blink & 2))) {
      if (ducking && grounded)
        dinoDrawSprite(DINO_TREX_X, trexFeet - 12, dinoDuck, 20, 12);
      else
        dinoDrawSprite(DINO_TREX_X, trexFeet - 20, (animFrm & 1) ? dinoRun2 : dinoRun1, 18, 20);
    }
    display.display();
  }
}

// ==================== TETRIS ====================
#define TET_COLS 10
#define TET_ROWS 20
#define TET_BLOCK 2
#define TET_FIELD_X 2
#define TET_FIELD_Y 2
#define TET_CLEAR_MS 420
#define TET_FLASH_MS 70
#define TET_BASE_DROP_MS 800
#define TET_MIN_DROP_MS 80
#define TET_DROP_STEP_MS 70
#define TET_LINES_PER_LEVEL 10
#define TET_REPEAT_DELAY_MS 170
#define TET_REPEAT_RATE_MS 55

void runTetris() {
  static const uint16_t PIECES[7][4] = {
    {0x0F00,0x2222,0x00F0,0x4444},{0x8E00,0x6440,0x0E20,0x44C0},
    {0x2E00,0x4460,0x0E80,0xC440},{0x6600,0x6600,0x6600,0x6600},
    {0x6C00,0x4620,0x06C0,0x8C40},{0x4E00,0x4640,0x0E40,0x4C40},
    {0xC600,0x2640,0x0C60,0x4C80}};
  enum { ST_PLAY, ST_CLEAR, ST_OVER } state;
  uint8_t board[TET_ROWS][TET_COLS];
  uint8_t pType, pRot, nType;
  int8_t pX, pY;
  uint8_t clearRows[4], clearCount = 0;
  uint32_t clearStart = 0, flashTimer = 0, dropTimer = 0, dropInterval;
  uint32_t score = 0;
  uint16_t lines = 0;
  uint8_t level = 0;

  auto cellSet = [](uint16_t s, int r, int c) { return (s >> (15 - (r*4 + c))) & 1; };
  auto collides = [&](uint8_t t, uint8_t rot, int8_t x, int8_t y) {
    uint16_t s = PIECES[t][rot];
    for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) {
      if (!cellSet(s, r, c)) continue;
      int bx = x + c, by = y + r;
      if (bx < 0 || bx >= TET_COLS || by >= TET_ROWS) return true;
      if (by >= 0 && board[by][bx]) return true;
    }
    return false;
  };
  auto spawn = [&]() {
    pType = nType; nType = random(0, 7); pRot = 0; pX = 3;
    uint16_t s = PIECES[pType][0]; pY = 0;
    for (int r = 0; r < 4; r++) {
      bool f = false; for (int c = 0; c < 4; c++) if (cellSet(s, r, c)) { f = true; break; }
      if (f) { pY = -r; break; }
    }
    if (collides(pType, pRot, pX, pY)) state = ST_OVER;
    else { state = ST_PLAY; dropTimer = millis(); }
  };
  auto lock = [&]() {
    uint16_t s = PIECES[pType][pRot];
    for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) {
      if (!cellSet(s, r, c)) continue;
      int bx = pX + c, by = pY + r;
      if (by >= 0 && by < TET_ROWS && bx >= 0 && bx < TET_COLS) board[by][bx] = 1;
    }
    clearCount = 0;
    for (int r = 0; r < TET_ROWS; r++) {
      bool full = true;
      for (int c = 0; c < TET_COLS; c++) if (!board[r][c]) { full = false; break; }
      if (full && clearCount < 4) clearRows[clearCount++] = r;
    }
    if (clearCount > 0) { state = ST_CLEAR; clearStart = millis(); flashTimer = clearStart; }
    else spawn();
  };
  auto applyScore = [&](uint8_t n) {
    static const uint16_t pts[5] = {0, 100, 300, 500, 800};
    if (n > 4) n = 4;
    score += (uint32_t)pts[n] * (uint32_t)(level + 1);
    lines += n;
    uint8_t nl = lines / TET_LINES_PER_LEVEL;
    if (nl != level) {
      level = nl;
      dropInterval = TET_BASE_DROP_MS - (uint32_t)level * TET_DROP_STEP_MS;
      if (dropInterval < TET_MIN_DROP_MS) dropInterval = TET_MIN_DROP_MS;
    }
  };
  auto reset = [&]() {
    memset(board, 0, sizeof(board));
    score = 0; lines = 0; level = 0; dropInterval = TET_BASE_DROP_MS;
    clearCount = 0; nType = random(0, 7); spawn(); dropTimer = millis();
  };
  auto drawPreview = [&](uint8_t type) {
    uint16_t s = PIECES[type][0];
    int minR = 4, maxR = -1, minC = 4, maxC = -1;
    for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) if (cellSet(s, r, c)) {
      if (r < minR) minR = r; if (r > maxR) maxR = r;
      if (c < minC) minC = c; if (c > maxC) maxC = c;
    }
    if (maxR < 0) return;
    const int BS = 4;
    int w = (maxC - minC + 1) * BS, h = (maxR - minR + 1) * BS;
    int ox = 35 + (20 - w) / 2, oy = 9 + (20 - h) / 2;
    for (int r = minR; r <= maxR; r++) for (int c = minC; c <= maxC; c++)
      if (cellSet(s, r, c)) display.fillRect(ox + (c-minC)*BS, oy + (r-minR)*BS, BS, BS, SSD1306_WHITE);
  };
  auto drawField = [&]() {
    display.drawRect(TET_FIELD_X - 1, TET_FIELD_Y - 1, TET_COLS*TET_BLOCK + 2, TET_ROWS*TET_BLOCK + 2, SSD1306_WHITE);
    bool flash = false;
    if (state == ST_CLEAR) flash = ((millis() - clearStart) / TET_FLASH_MS) & 1;
    for (int r = 0; r < TET_ROWS; r++) {
      bool rowClr = false;
      if (state == ST_CLEAR) for (int i = 0; i < clearCount; i++) if (clearRows[i] == r) { rowClr = true; break; }
      if (rowClr && flash) continue;
      for (int c = 0; c < TET_COLS; c++) if (board[r][c])
        display.fillRect(TET_FIELD_X + c*TET_BLOCK, TET_FIELD_Y + r*TET_BLOCK, TET_BLOCK, TET_BLOCK, SSD1306_WHITE);
    }
    if (state == ST_PLAY) {
      uint16_t s = PIECES[pType][pRot];
      for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) {
        if (!cellSet(s, r, c)) continue;
        int bx = pX + c, by = pY + r;
        if (by < 0 || by >= TET_ROWS || bx < 0 || bx >= TET_COLS) continue;
        display.fillRect(TET_FIELD_X + bx*TET_BLOCK, TET_FIELD_Y + by*TET_BLOCK, TET_BLOCK, TET_BLOCK, SSD1306_WHITE);
      }
    }
  };
  auto drawPanel = [&]() {
    char buf[16];
    display.setTextSize(1); display.setCursor(36, 0); display.print(F("NEXT"));
    display.drawRect(34, 8, 22, 22, SSD1306_WHITE);
    drawPreview(nType);
    display.setCursor(60, 8); display.print(F("SCORE"));
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)score);
    display.setTextSize(2); display.setCursor(60, 18); display.print(buf);
    display.setTextSize(1); display.setCursor(60, 36); display.print(F("LEVEL"));
    snprintf(buf, sizeof(buf), "%u", (unsigned)level);
    display.setTextSize(2); display.setCursor(60, 46); display.print(buf);
  };
  auto render = [&]() {
    clearDisp();
    if (state == ST_OVER) {
      char buf[24];
      display.setTextSize(2); display.setCursor(10, 8); display.print(F("GAME OVER"));
      display.setTextSize(1);
      snprintf(buf, sizeof(buf), "SCORE %lu", (unsigned long)score);
      display.setCursor(10, 34); display.print(buf);
      display.setCursor(10, 46); display.print(F("UP/R:Restart"));
      display.setCursor(10, 56); display.print(F("Hold LEFT:Exit"));
    } else { drawField(); drawPanel(); }
    display.display();
  };

  reset();
  logEvent("TETRIS ON");
  clearBtnFlags();
  unsigned long lastL = 0, lastR = 0, lastD = 0;
  bool prevL = false, prevR = false, prevD = false, prevU = false;

  while (true) {
    uint32_t now = millis();
    updateButtons();
    if (btn[BI_LEFT].longFired) { btn[BI_LEFT].longFired = false; clearBtnFlags(); return; }
    bool lh = isButtonDown(BI_LEFT), rh = isButtonDown(BI_RIGHT),
         dh = isButtonDown(BI_DOWN), uh = isButtonDown(BI_UP);
    bool ue = uh && !prevU; prevU = uh;

    if (btn[BI_RIGHT].longFired) {
      btn[BI_RIGHT].longFired = false;
      if (state == ST_PLAY) { while (!collides(pType, pRot, pX, pY+1)) { pY++; score += 2; } lock(); }
    }

    if (state == ST_OVER) {
      bool re = rh && !prevR; prevR = rh;
      if (ue || re) reset();
      render(); delay(20); continue;
    }

    if (state == ST_PLAY) {
      if (ue) {
        uint8_t nr = (pRot + 1) & 3;
        if (!collides(pType, nr, pX, pY)) pRot = nr;
        else { const int8_t k[4] = {-1,1,-2,2};
          for (int i = 0; i < 4; i++) if (!collides(pType, nr, pX + k[i], pY)) { pRot = nr; pX += k[i]; break; } }
      }
      if (lh) {
        bool fire = false;
        if (!prevL) { fire = true; lastL = now + TET_REPEAT_DELAY_MS; }
        else if ((int32_t)(now - lastL) >= 0) { fire = true; lastL = now + TET_REPEAT_RATE_MS; }
        if (fire && !collides(pType, pRot, pX - 1, pY)) pX--;
      }
      prevL = lh;
      if (rh && !btn[BI_RIGHT].longFired) {
        bool fire = false;
        if (!prevR) { fire = true; lastR = now + TET_REPEAT_DELAY_MS; }
        else if ((int32_t)(now - lastR) >= 0) { fire = true; lastR = now + TET_REPEAT_RATE_MS; }
        if (fire && !collides(pType, pRot, pX + 1, pY)) pX++;
      }
      prevR = rh;
      if (dh) {
        bool fire = false;
        if (!prevD) { fire = true; lastD = now + TET_REPEAT_DELAY_MS; }
        else if ((int32_t)(now - lastD) >= 0) { fire = true; lastD = now + TET_REPEAT_RATE_MS; }
        if (fire) {
          if (!collides(pType, pRot, pX, pY + 1)) { pY++; score += 1; }
          else lock();
        }
      }
      prevD = dh;
      if (state == ST_PLAY && (now - dropTimer >= dropInterval)) {
        dropTimer = now;
        if (!collides(pType, pRot, pX, pY + 1)) pY++;
        else lock();
      }
    } else if (state == ST_CLEAR) {
      if (now - clearStart >= TET_CLEAR_MS) {
        uint8_t n = clearCount;
        for (int i = 0; i < clearCount - 1; i++) for (int j = i + 1; j < clearCount; j++)
          if (clearRows[j] < clearRows[i]) { uint8_t t = clearRows[i]; clearRows[i] = clearRows[j]; clearRows[j] = t; }
        for (int i = clearCount - 1; i >= 0; i--) {
          int row = clearRows[i];
          for (int r = row; r > 0; r--) for (int c = 0; c < TET_COLS; c++) board[r][c] = board[r-1][c];
          for (int c = 0; c < TET_COLS; c++) board[0][c] = 0;
        }
        applyScore(n); spawn();
      }
    }
    render();
    delay(8);
  }
}

// ==================== SNAKE ====================
void runSnake() {
  #define SNK_COLS 24
  #define SNK_ROWS 8
  #define SNK_CELL 5
  #define SNK_OFF_X 4
  #define SNK_OFF_Y 13
  #define SNK_MAX_LEN 100
  #define SNK_START_STEP_MS 220
  #define SNK_MIN_STEP_MS 70
  #define SNK_STEP_DEC 6

  struct Seg { int8_t x, y; };
  Seg body[SNK_MAX_LEN];
  Seg prev[SNK_MAX_LEN];
  uint16_t len = 0, lenPrev = 0;
  int8_t dirX = 1, dirY = 0, pendX = 1, pendY = 0;
  int8_t foodX = 0, foodY = 0;
  uint16_t score = 0, hiScore = 0;
  uint32_t stepMs = SNK_START_STEP_MS, lastStep = 0;
  enum { ST_MENU, ST_PLAY, ST_PAUSE, ST_OVER } state = ST_MENU;
  uint32_t stateTimer = 0;

  prefs.begin("snake", true);
  hiScore = prefs.getUShort("hi", 0);
  prefs.end();
  if (hiScore == 0xFFFF) hiScore = 0;

  auto reset = [&]() {
    len = 4;
    for (int i = 0; i < len; i++) { body[i].x = 6 - i; body[i].y = SNK_ROWS/2; }
    memcpy(prev, body, sizeof(Seg) * len);
    lenPrev = len;
    dirX = 1; dirY = 0; pendX = 1; pendY = 0;
    score = 0; stepMs = SNK_START_STEP_MS; lastStep = millis();
    int tries = 0; bool col;
    do {
      col = false;
      foodX = random(0, SNK_COLS); foodY = random(0, SNK_ROWS);
      for (int i = 0; i < len; i++) if (body[i].x == foodX && body[i].y == foodY) { col = true; break; }
      tries++;
    } while (col && tries < 100);
  };

  reset();
  state = ST_MENU;
  logEvent("SNAKE ON");
  clearBtnFlags();
  unsigned long lastFrame = millis();

  while (true) {
    Act a = getAction();
    if (a == A_BACK) { clearBtnFlags(); return; }

    if (state == ST_MENU) {
      if (a == A_RIGHT) { reset(); state = ST_PLAY; lastStep = millis(); clearBtnFlags(); }
    } else if (state == ST_PLAY) {
      if (a == A_UP && dirY == 0)        { pendX = 0; pendY = -1; }
      else if (a == A_DOWN && dirY == 0) { pendX = 0; pendY = 1; }
      else if (a == A_LEFT && dirX == 0) { pendX = -1; pendY = 0; }
      else if (a == A_RIGHT && dirX == 0){ pendX = 1; pendY = 0; }
      else if (a == A_ALT_RIGHT)         { state = ST_PAUSE; stateTimer = millis(); }

      uint32_t now = millis();
      if (now - lastStep >= stepMs) {
        lastStep = now;
        memcpy(prev, body, sizeof(Seg) * len);
        lenPrev = len;
        dirX = pendX; dirY = pendY;
        int8_t nx = body[0].x + dirX, ny = body[0].y + dirY;
        bool dead = false;
        if (nx < 0 || nx >= SNK_COLS || ny < 0 || ny >= SNK_ROWS) dead = true;
        else for (int i = 0; i < len - 1; i++) if (body[i].x == nx && body[i].y == ny) { dead = true; break; }
        if (dead) {
          state = ST_OVER; stateTimer = millis();
          if (score > hiScore) {
            hiScore = score;
            prefs.begin("snake", false); prefs.putUShort("hi", hiScore); prefs.end();
          }
        } else {
          bool ate = (nx == foodX && ny == foodY);
          if (!ate) {
            for (int i = len - 1; i > 0; i--) body[i] = body[i - 1];
            body[0].x = nx; body[0].y = ny;
          } else {
            if (len < SNK_MAX_LEN) {
              for (int i = len; i > 0; i--) body[i] = body[i - 1];
              len++;
              body[0].x = nx; body[0].y = ny;
            }
            score++;
            if (stepMs > SNK_MIN_STEP_MS + SNK_STEP_DEC) stepMs -= SNK_STEP_DEC;
            else stepMs = SNK_MIN_STEP_MS;
            int tries = 0; bool col;
            do {
              col = false;
              foodX = random(0, SNK_COLS); foodY = random(0, SNK_ROWS);
              for (int i = 0; i < len; i++) if (body[i].x == foodX && body[i].y == foodY) { col = true; break; }
              tries++;
            } while (col && tries < 200);
          }
        }
      }
    } else if (state == ST_PAUSE) {
      if (a == A_ALT_RIGHT || a == A_RIGHT) { state = ST_PLAY; lastStep = millis(); }
      if (millis() - stateTimer > 6000) { clearBtnFlags(); return; }
    } else if (state == ST_OVER) {
      if (a == A_RIGHT) { reset(); state = ST_PLAY; lastStep = millis(); clearBtnFlags(); }
      if (millis() - stateTimer > 5000) { clearBtnFlags(); return; }
    }

    if (millis() - lastFrame >= 16) {
      lastFrame = millis();
      float t = 0.0f;
      if (state == ST_PLAY) {
        t = (float)(millis() - lastStep) / (float)stepMs;
        if (t > 1.0f) t = 1.0f;
      }
      clearDisp();
      display.setCursor(2, 1);
      display.print(F("SNK  HI:")); display.print(hiScore); display.print(F("  ")); display.print(score);
      display.drawFastHLine(0, 10, 128, SSD1306_WHITE);
      display.drawRect(SNK_OFF_X - 1, SNK_OFF_Y - 1, SNK_COLS*SNK_CELL + 2, SNK_ROWS*SNK_CELL + 2, SSD1306_WHITE);
      int fx = SNK_OFF_X + foodX * SNK_CELL + SNK_CELL/2;
      int fy = SNK_OFF_Y + foodY * SNK_CELL + SNK_CELL/2;
      float pulse = 0.5f + 0.5f * sinf(millis() / 200.0f);
      display.fillCircle(fx, fy, 1 + (int)(pulse * 1.5f), SSD1306_WHITE);
      for (int i = (int)len - 1; i >= 0; i--) {
        float x, y;
        if (i < (int)lenPrev && i < (int)len) {
          x = prev[i].x + (body[i].x - prev[i].x) * t;
          y = prev[i].y + (body[i].y - prev[i].y) * t;
        } else { x = body[i].x; y = body[i].y; }
        int px = SNK_OFF_X + (int)(x * SNK_CELL);
        int py = SNK_OFF_Y + (int)(y * SNK_CELL);
        display.fillRect(px, py, SNK_CELL - 1, SNK_CELL - 1, SSD1306_WHITE);
      }
      if (state == ST_MENU) {
        display.fillRect(20, 22, 88, 24, SSD1306_BLACK);
        display.drawRect(20, 22, 88, 24, SSD1306_WHITE);
        display.setCursor(52, 26); display.print(F("SNAKE"));
        display.setCursor(28, 36); display.print(F("RIGHT: Start"));
        drawHint("L-LEFT:Exit");
      } else if (state == ST_PAUSE) {
        display.fillRect(20, 22, 88, 24, SSD1306_BLACK);
        display.drawRect(20, 22, 88, 24, SSD1306_WHITE);
        display.setCursor(46, 26); display.print(F("PAUSED"));
        display.setCursor(26, 36); display.print(F("RIGHT: Resume"));
        drawHint("L-RIGHT:Pause L-LEFT:Exit");
      } else if (state == ST_OVER) {
        display.fillRect(14, 18, 100, 32, SSD1306_BLACK);
        display.drawRect(14, 18, 100, 32, SSD1306_WHITE);
        display.setCursor(28, 22); display.print(F("GAME OVER"));
        display.setCursor(24, 32); display.print(F("RIGHT: Restart"));
        drawHint("L-LEFT:Exit");
      }
      display.display();
    }
  }
}

// ==================== GAME MENU ====================
const char *GAMES_ITEMS[] = {"Space Shooter", "Pong", "Dino Runner", "Tetris", "Snake"};
const uint8_t *GAMES_ICONS[] = {ICON_SHIP, ICON_PONG, ICON_DINO, ICON_TETRIS, ICON_SNAKE};
#define GAMES_COUNT 5

void runGameMenu() {
  clearBtnFlags();
  int sel = 0;
  while (true) {
    clearDisp();
    drawMiniHeader("GAMES");
    for (int i = 0; i < GAMES_COUNT; i++) {
      int y = 13 + i * 10;
      bool selected = (i == sel);
      if (selected) {
        display.fillRect(0, y - 1, 122, 10, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK);
      }
      display.drawBitmap(4, y, GAMES_ICONS[i], 8, 8, SSD1306_WHITE);
      display.setCursor(16, y);
      display.print(GAMES_ITEMS[i]);
      display.setTextColor(SSD1306_WHITE);
    }
    display.display();
    Act a = getAction();
    if (a == A_UP)    sel = (sel - 1 + GAMES_COUNT) % GAMES_COUNT;
    if (a == A_DOWN)  sel = (sel + 1) % GAMES_COUNT;
    if (a == A_BACK)  { clearBtnFlags(); return; }
    if (a == A_RIGHT) {
      clearBtnFlags();
      switch (sel) {
        case 0: runShooter(); break;
        case 1: runPong();    break;
        case 2: runDino();    break;
        case 3: runTetris();  break;
        case 4: runSnake();   break;
      }
      clearBtnFlags();
    }
    delay(30);
  }
}

// ==================== TIMER APP ====================
void runTimer() {
  static const uint16_t PRESETS[] = {300, 900, 1500, 2700, 3600};
  static const char *NAMES[] = {"5m Break", "15m Quick", "25m Study", "45m Lecture", "60m Exam"};
  clearBtnFlags();
  int idx = 2;
  while (true) {
    clearDisp();
    drawMiniHeader("TIMER");
    display.setCursor(10, 22); display.print(NAMES[idx]);
    display.setTextSize(2); display.setCursor(44, 36); display.print(PRESETS[idx] / 60);
    display.setTextSize(1); display.setCursor(76, 42); display.print("min");
    display.display();
    Act a = getAction();
    if (a == A_UP)   idx = (idx + 1) % 5;
    if (a == A_DOWN) idx = (idx - 1 + 5) % 5;
    if (a == A_BACK) { clearBtnFlags(); return; }
    if (a == A_RIGHT) {
      uint32_t total = PRESETS[idx], rem = total;
      unsigned long tick = millis();
      while (rem > 0) {
        Act a2 = getAction();
        if (a2 == A_BACK) { clearBtnFlags(); return; }
        if (millis() - tick >= 1000) { tick = millis(); rem--; }
        clearDisp();
        drawMiniHeader("TIMER");
        display.setTextSize(2); display.setCursor(38, 18);
        int m = rem / 60, s = rem % 60;
        if (m < 10) display.print('0'); display.print(m);
        display.print(':');
        if (s < 10) display.print('0'); display.print(s);
        int cx = 64, cy = 48, radius = 10;
        display.drawCircle(cx, cy, radius, SSD1306_WHITE);
        float progress = (float)rem / total;
        float endA = -PI/2 + progress * 2 * PI;
        float angle = -PI/2;
        while (angle < endA) {
          display.drawPixel(cx + cos(angle)*radius, cy + sin(angle)*radius, SSD1306_WHITE);
          display.drawPixel(cx + cos(angle)*(radius-1), cy + sin(angle)*(radius-1), SSD1306_WHITE);
          angle += 0.15f;
        }
        display.display();
        delay(30);
      }
      for (int i = 0; i < 6; i++) {
        display.invertDisplay(true);  delay(200);
        display.invertDisplay(false); delay(200);
      }
      clearDisp();
      display.setTextSize(2); display.setCursor(14, 26); display.print(F("TIME UP"));
      display.display();
      delay(1500);
    }
    delay(30);
  }
}

// ==================== SLEEP APP ====================
void runSleep() {
  logEvent("SLEEP");
  for (int i = 0; i < 3; i++) {
    display.invertDisplay(true);  delay(30);
    display.invertDisplay(false); delay(30);
  }
  clearDisp(); display.display();
  display.ssd1306_command(SSD1306_DISPLAYOFF);
  clearBtnFlags();
  while (getAction() == A_NONE) delay(50);
  display.ssd1306_command(SSD1306_DISPLAYON);
  applyBrightness();
  clearBtnFlags();
}

// ==================== APPS MENU ====================
const char *APPS_ITEMS[] = {"Timer", "Sleep"};
const uint8_t *APPS_ICONS[] = {ICON_TIMER, ICON_SLEEP};
#define APPS_COUNT 2

void runAppsMenu() {
  clearBtnFlags();
  int sel = 0;
  while (true) {
    clearDisp();
    drawMiniHeader("APPS");
    for (int i = 0; i < APPS_COUNT; i++) {
      int y = 22 + i * 12;
      bool selected = (i == sel);
      if (selected) {
        display.fillRect(0, y - 1, 124, 11, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK);
      }
      display.drawBitmap(6, y + 1, APPS_ICONS[i], 8, 8, SSD1306_WHITE);
      display.setCursor(20, y + 1);
      display.print(APPS_ITEMS[i]);
      display.setTextColor(SSD1306_WHITE);
    }
    display.display();
    Act a = getAction();
    if (a == A_UP)    sel = (sel - 1 + APPS_COUNT) % APPS_COUNT;
    if (a == A_DOWN)  sel = (sel + 1) % APPS_COUNT;
    if (a == A_BACK)  { clearBtnFlags(); return; }
    if (a == A_RIGHT) {
      clearBtnFlags();
      if (sel == 0) runTimer(); else runSleep();
      clearBtnFlags();
    }
    delay(30);
  }
}

// ==================== SETTINGS SCREENS ====================
void runBrightness() {
  clearBtnFlags();
  const char *lvls[] = {"25%", "50%", "75%", "100%"};
  while (true) {
    clearDisp();
    drawMiniHeader("Brightness");
    for (int i = 0; i < 4; i++) {
      int y = 18 + i * 9;
      if (i == cfg.brightness) {
        display.fillRect(0, y - 1, 100, 9, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK);
      }
      display.setCursor(6, y); display.print(lvls[i]);
      display.setTextColor(SSD1306_WHITE);
    }
    display.display();
    Act a = getAction();
    bool changed = false;
    if (a == A_UP)   { cfg.brightness = (cfg.brightness - 1 + 4) % 4; changed = true; }
    if (a == A_DOWN) { cfg.brightness = (cfg.brightness + 1) % 4;     changed = true; }
    if (changed) applyBrightness();
    if (a == A_RIGHT || a == A_BACK) { saveCfg(); clearBtnFlags(); return; }
    delay(30);
  }
}

void runTimeout() {
  clearBtnFlags();
  uint32_t TO[] = {5000, 10000, 20000, 30000, 60000, 0};
  const char *TN[] = {"5s", "10s", "20s", "30s", "60s", "Never"};
  int count = 6, sel = 0;
  for (int i = 0; i < count; i++) if (TO[i] == cfg.menuTimeoutMs) sel = i;
  while (true) {
    clearDisp();
    drawMiniHeader("Timeout");
    for (int i = 0; i < count; i++) {
      int y = 14 + i * 7;
      if (i == sel) {
        display.fillRect(0, y - 1, 100, 7, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK);
      }
      display.setCursor(6, y); display.print(TN[i]);
      display.setTextColor(SSD1306_WHITE);
    }
    display.display();
    Act a = getAction();
    if (a == A_UP)   sel = (sel - 1 + count) % count;
    if (a == A_DOWN) sel = (sel + 1) % count;
    if (a == A_RIGHT) { cfg.menuTimeoutMs = TO[sel]; saveCfg(); clearBtnFlags(); return; }
    if (a == A_BACK)  { clearBtnFlags(); return; }
    delay(30);
  }
}

void runFlip() {
  clearBtnFlags();
  while (true) {
    clearDisp();
    drawMiniHeader("Screen Flip");
    display.setCursor(0, 22); display.print(F("Flip 180:"));
    display.setTextSize(2); display.setCursor(20, 34);
    display.print(cfg.screenFlip ? F("ON") : F("OFF"));
    display.setTextSize(1);
    display.display();
    Act a = getAction();
    if (a == A_RIGHT) {
      cfg.screenFlip = !cfg.screenFlip;
      applyScreenFlip();
      saveCfg();
    }
    if (a == A_BACK) { saveCfg(); clearBtnFlags(); return; }
    delay(30);
  }
}

// ==================== DIAGNOSTICS ====================
void runDiagnostics() {
  clearBtnFlags();
  unsigned long lastUpdate = 0;
  uint8_t page = 0;
  while (true) {
    Act a = getAction();
    if (a == A_BACK) { clearBtnFlags(); return; }
    if (a == A_UP || a == A_DOWN) page = (page + 1) % 3;
    if (millis() - lastUpdate > 400) {
      lastUpdate = millis();
      clearDisp();
      drawMiniHeader("DIAGNOSTICS");
      char buf[32];
      if (page == 0) {
        // Memory + storage
        display.setCursor(0, 13);
        snprintf(buf, sizeof(buf), "Free:  %u", (unsigned)ESP.getFreeHeap());
        display.print(buf);
        display.setCursor(0, 22);
        snprintf(buf, sizeof(buf), "Min:   %u", (unsigned)minFreeHeap);
        display.print(buf);
        display.setCursor(0, 31);
        prefs.begin("neon", true);
        size_t ne = prefs.freeEntries();
        prefs.end();
        snprintf(buf, sizeof(buf), "NVS:   %u free", (unsigned)ne);
        display.print(buf);
        display.setCursor(0, 40);
        snprintf(buf, sizeof(buf), "Sketch:%u B", (unsigned)ESP.getSketchSize());
        display.print(buf);
        display.setCursor(0, 49);
        snprintf(buf, sizeof(buf), "Boots: %lu", (unsigned long)bootCount);
        display.print(buf);
        display.setCursor(0, 57);
        snprintf(buf, sizeof(buf), "Clean: %lu", (unsigned long)cleanupCount);
        display.print(buf);
      } else if (page == 1) {
        // CPU + loop
        display.setCursor(0, 13);
        snprintf(buf, sizeof(buf), "CPU:  %u MHz", (unsigned)getCpuFrequencyMhz());
        display.print(buf);
        display.setCursor(0, 22);
        snprintf(buf, sizeof(buf), "Loop: %lu us", (unsigned long)loopTimeUs);
        display.print(buf);
        display.setCursor(0, 31);
        uint32_t sec = millis() / 1000;
        snprintf(buf, sizeof(buf), "Up:   %lum %lus", sec/60, sec%60);
        display.print(buf);
        display.setCursor(0, 40);
        snprintf(buf, sizeof(buf), "Frames:%lu", (unsigned long)frameCount);
        display.print(buf);
        display.setCursor(0, 49);
        snprintf(buf, sizeof(buf), "WiFi: OFF");
        display.print(buf);
        display.setCursor(0, 57);
        snprintf(buf, sizeof(buf), "Reset: manual");
        display.print(buf);
      } else {
        // Battery + status
        display.setCursor(0, 13);
        snprintf(buf, sizeof(buf), "Batt: %.2f V", batteryVoltage);
        display.print(buf);
        display.setCursor(0, 22);
        snprintf(buf, sizeof(buf), "Level:%u%%", (unsigned)batteryPercent);
        display.print(buf);
        drawProgressBar(0, 32, 100, 8, batteryPercent);
        display.drawBitmap(6, 44, ICON_BATTERY, 8, 8, SSD1306_WHITE);
        display.setCursor(20, 45);
        snprintf(buf, sizeof(buf), "Batt %u%%", (unsigned)batteryPercent);
        display.print(buf);
        display.setCursor(0, 57);
        display.print(F("U/D: page L-long:Exit"));
      }
      display.display();
    }
    delay(30);
  }
}

// ==================== LOGS VIEWER ====================
void runLogs() {
  clearBtnFlags();
  int scroll = 0;
  while (true) {
    clearDisp();
    drawMiniHeader("LOGS");
    int visible = 6;
    int total = logFill;
    int start = scroll;
    if (start > total - visible) start = total - visible;
    if (start < 0) start = 0;
    if (total == 0) {
      display.setCursor(6, 26);
      display.print(F("(no events)"));
    } else {
      for (int i = 0; i < visible; i++) {
        int idx = (logHead - total + start + i + LOG_MAX * 2) % LOG_MAX;
        if (start + i >= total) break;
        display.setCursor(0, 12 + i * 8);
        char line[32];
        snprintf(line, sizeof(line), "%lus %s",
                 (unsigned long)logs[idx].t, logs[idx].msg);
        display.print(line);
      }
    }
    display.display();
    Act a = getAction();
    if (a == A_UP)   scroll = (scroll - 1 < 0) ? 0 : scroll - 1;
    if (a == A_DOWN) scroll++;
    if (a == A_BACK) { clearBtnFlags(); return; }
    delay(30);
  }
}

// ==================== MEMORY TOOLS ====================
void runMemoryTools() {
  clearBtnFlags();
  int sel = 0;
  const char *items[] = {"Auto-fix now", "Clear logs", "Wipe tmp NVS", "Back"};
  while (true) {
    clearDisp();
    drawMiniHeader("MEMORY TOOLS");
    display.setCursor(0, 12);
    char buf[32];
    snprintf(buf, sizeof(buf), "Free: %u  Min: %u",
             (unsigned)ESP.getFreeHeap(), (unsigned)minFreeHeap);
    display.print(buf);
    for (int i = 0; i < 4; i++) {
      int y = 22 + i * 9;
      bool s = (i == sel);
      if (s) {
        display.fillRect(0, y - 1, 100, 9, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK);
      }
      display.setCursor(4, y); display.print(items[i]);
      display.setTextColor(SSD1306_WHITE);
    }
    display.display();
    Act a = getAction();
    if (a == A_UP)   sel = (sel - 1 + 4) % 4;
    if (a == A_DOWN) sel = (sel + 1) % 4;
    if (a == A_BACK) { clearBtnFlags(); return; }
    if (a == A_RIGHT) {
      if (sel == 0) { autoFixMemory(); logEvent("MANUAL FIX"); }
      else if (sel == 1) { logHead = 0; logFill = 0; logEvent("LOG CLEAR"); }
      else if (sel == 2) {
        prefs.begin("tmp", false); prefs.clear(); prefs.end();
        logEvent("TMP WIPED");
      } else { clearBtnFlags(); return; }
      delay(400);
    }
    delay(20);
  }
}

// ==================== REBOOT ====================
void runReboot() {
  clearBtnFlags();
  while (true) {
    clearDisp();
    drawMiniHeader("REBOOT");
    display.setCursor(6, 20);
    display.print(F("Confirm reboot?"));
    display.setCursor(6, 34);
    display.print(F("RIGHT: reboot"));
    display.setCursor(6, 46);
    display.print(F("LEFT-long: cancel"));
    display.display();
    Act a = getAction();
    if (a == A_BACK) { clearBtnFlags(); return; }
    if (a == A_RIGHT) {
      logEvent("REBOOT CMD");
      saveCfg();
      delay(300);
      ESP.restart();
    }
    delay(30);
  }
}

// ==================== SETTINGS MENU ====================
const char *SETTINGS_ITEMS[] = {
  "Brightness", "Timeout", "Screen Flip",
  "Diagnostics", "Logs", "Memory", "Reboot"
};
#define SET_COUNT 7

void runSettings() {
  clearBtnFlags();
  int sel = 0;
  while (true) {
    clearDisp();
    drawMiniHeader("SETTINGS");
    int visible = 5;
    int start = sel - visible / 2;
    if (start < 0) start = 0;
    if (start > SET_COUNT - visible) start = SET_COUNT - visible;
    if (start < 0) start = 0;
    for (int i = 0; i < visible && (start + i) < SET_COUNT; i++) {
      int idx = start + i, y = 13 + i * 10;
      bool selected = (idx == sel);
      if (selected) {
        display.fillRect(0, y - 1, 122, 10, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK);
      }
      display.setCursor(4, y);
      display.print(selected ? "> " : "  ");
      display.print(SETTINGS_ITEMS[idx]);
      display.setTextColor(SSD1306_WHITE);
    }
    display.display();
    Act a = getAction();
    if (a == A_UP)    sel = (sel - 1 + SET_COUNT) % SET_COUNT;
    if (a == A_DOWN)  sel = (sel + 1) % SET_COUNT;
    if (a == A_BACK)  { clearBtnFlags(); return; }
    if (a == A_RIGHT) {
      clearBtnFlags();
      switch (sel) {
        case 0: runBrightness();  break;
        case 1: runTimeout();     break;
        case 2: runFlip();        break;
        case 3: runDiagnostics(); break;
        case 4: runLogs();        break;
        case 5: runMemoryTools(); break;
        case 6: runReboot();      break;
      }
      clearBtnFlags();
    }
    delay(20);
  }
}

// ==================== HOME MENU ====================
#define HOME_COUNT 3
const char *HOME_NAMES[HOME_COUNT] = {"Games", "Apps", "Settings"};
const uint8_t *HOME_ICONS[HOME_COUNT] = {ICON_GAMES, ICON_APPS, ICON_GEAR};
int homeSel = 0;

void renderHome() {
  clearDisp();
  display.setCursor(2, 1); display.print(F("NEON OS"));
  unsigned long upt = millis() / 1000;
  char t[12];
  snprintf(t, sizeof(t), "%02lu:%02lu", (upt/60)%60, upt%60);
  display.setCursor(80, 1); display.print(t);
  display.drawFastHLine(0, 10, 128, SSD1306_WHITE);

  // Status strip: battery + heap
  display.drawBitmap(2, 12, ICON_BATTERY, 8, 8, SSD1306_WHITE);
  char sb[16];
  snprintf(sb, sizeof(sb), "%u%%", (unsigned)batteryPercent);
  display.setCursor(12, 13); display.print(sb);
  snprintf(sb, sizeof(sb), "H:%uK", (unsigned)(ESP.getFreeHeap()/1024));
  display.setCursor(52, 13); display.print(sb);

  for (int i = 0; i < HOME_COUNT; i++) {
    int y = 24 + i * 12;
    bool sel = (i == homeSel);
    if (sel) {
      display.fillRect(4, y - 1, 120, 11, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    }
    display.drawBitmap(8, y + 1, HOME_ICONS[i], 8, 8, SSD1306_WHITE);
    display.setCursor(22, y + 1);
    display.print(HOME_NAMES[i]);
    display.setTextColor(SSD1306_WHITE);
  }
  display.display();
}

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  setCpuFrequencyMhz(80);

  for (int i = 0; i < 4; i++) pinMode(BTN_PINS[i], INPUT_PULLUP);
  pinMode(BUZZER, OUTPUT);

  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(400000);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("OLED FAILED");
    while (1) delay(1000);
  }
  display.cp437(true);
  display.clearDisplay();
  display.display();

  loadCfg();
  applyBrightness();
  applyScreenFlip();
  loadBootCounter();
  autoCleanupTempMemory();
  minFreeHeap = ESP.getFreeHeap();

  randomSeed(esp_random());
  logEvent("BOOT OK");

  // Boot splash: single wallpaper (1.2s)
  display.clearDisplay();
  display.drawBitmap(0, 0, wallpaper, 128, 64, SSD1306_WHITE);
  display.display();
  delay(1200);

  homeSel = 0;
  lastActivity = millis();
  clearBtnFlags();
}

// ==================== MAIN LOOP ====================
void loop() {
  uint32_t t0 = micros();
  frameCount++;

  updateBattery();

  Act a = getAction();
  if (a == A_UP)   homeSel = (homeSel - 1 + HOME_COUNT) % HOME_COUNT;
  if (a == A_DOWN) homeSel = (homeSel + 1) % HOME_COUNT;
  if (a == A_RIGHT) {
    int r = homeSel;
    clearBtnFlags();
    switch (r) {
      case 0: runGameMenu(); break;
      case 1: runAppsMenu(); break;
      case 2: runSettings(); break;
    }
    clearBtnFlags();
  }
  renderHome();

  // Idle sleep
  if (cfg.menuTimeoutMs > 0 && (millis() - lastActivity) > cfg.menuTimeoutMs) {
    runSleep();
    lastActivity = millis();
  }

  autoFixMemory();

  loopTimeUs = micros() - t0;
  delay(16);
}
