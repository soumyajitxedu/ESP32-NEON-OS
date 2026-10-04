// ============================================================
//  NEON OS v3.3 — 4-BUTTON + SNAKE + TANJIRO + CAT NIGHT + CAT APP
//  ESP32 + SSD1306 128x64 + 4 buttons
//  GPIO 32=UP  33=DOWN  27=LEFT (long=BACK)  26=RIGHT
// ============================================================
// SAFETY / RESOURCE NOTES
// - The 18 stats and 3 plants use significant RAM. If boot reports
//   LoadProhibited/Guru Meditation, reduce CatPetData::garden to one slot.
// - Cat state saves every 5 minutes and on Cat app exit (roughly a 10-year
//   lifespan estimate under normal use; actual NVS/flash endurance varies).
//   Do not increase save frequency casually.
// - UI loops yield at most 50 ms. Avoid blocking work over 5 seconds or the
//   ESP32 watchdog may reset the device.
// - Behaviors without dedicated poses fall back to the existing sit/lay poses.
// - Cat Night uses sin() for tail motion (about 15% CPU as a rough estimate);
//   lower wallpaper frame rate if CPU headroom is needed by another feature.

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Preferences.h>
#include <math.h>
#include "neon_scene_api.h"
#include "cat_status_icons.h"
#include "cat_pet_types.h"
#include "generated_assets.h"
#include "wallpaper.h"

#define SCREEN_W 128
#define SCREEN_H 64
#define OLED_ADDR 0x3C
#define SDA_PIN 21
#define SCL_PIN 22

#define BTN_UP 32
#define BTN_DOWN 33
#define BTN_LEFT 27
#define BTN_RIGHT 26

const uint8_t BTN_PINS[4] = {BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT};
const unsigned long DEBOUNCE_MS = 40;
const unsigned long LONG_PRESS_MS = 1200;

#define BI_UP 0
#define BI_DOWN 1
#define BI_LEFT 2
#define BI_RIGHT 3

#define ARR_LEN(arr) (sizeof(arr) / sizeof((arr)[0]))

// ============================================================
//  TYPES
// ============================================================
struct Btn
{
    bool stable = HIGH;
    bool lastRead = HIGH;
    unsigned long lastChange = 0;
    unsigned long pressStart = 0;
    bool longFired = false;
    bool shortPressed = false;
    bool longPressed = false;
};

struct Settings
{
    uint8_t brightness = 3;
    uint32_t menuTimeoutMs = 20000;
    int8_t wallpaperIdx = 1; // -1=Off, 0=Tanjiro, 1=Cat Night
    bool screenFlip = false;
};

Adafruit_SSD1306 display(SCREEN_W, SCREEN_H, &Wire, -1);
Preferences prefs;
Btn btn[4];
Settings cfg;
unsigned long lastActivity = 0;

// ============================================================
//  STRING HELPERS
// ============================================================
static void pgmCopyTrunc(char *dest, size_t destSize, const char *pgmSrc)
{
    if (destSize == 0)
        return;
    strncpy_P(dest, pgmSrc, destSize - 1);
    dest[destSize - 1] = 0;
}

static int wrapText(const char *src, char out[][24], int maxLines, int maxCols)
{
    int line = 0, col = 0;
    out[0][0] = 0;
    const char *p = src;
    int wrapAt = maxCols - 3, softWrap = maxCols - 6;
    while (*p && line < maxLines)
    {
        if (col >= wrapAt || *p == '\n')
        {
            out[line][col] = 0;
            line++;
            col = 0;
            if (*p == '\n')
                p++;
            if (line < maxLines)
                out[line][0] = 0;
            continue;
        }
        if (col > 0 && *p == ' ' && *(p + 1) && col >= softWrap)
        {
            out[line][col] = 0;
            line++;
            col = 0;
            if (line < maxLines)
                out[line][0] = 0;
            p++;
            continue;
        }
        out[line][col++] = *p++;
    }
    if (line < maxLines)
    {
        out[line][col] = 0;
        line++;
    }
    return line;
}

// ============================================================
//  VAULT DATA
// ============================================================
struct Quote
{
    const char *category;
    const char *author;
    const char *text;
};

const Quote VAULT_QUOTES[] PROGMEM = {
    {"DSMP", "ParrotX2", "I'm not smarter than you. I'm not... this prison, the only reason it's perfect is because of that exact reason. Both of us know-you will deny it, but both of us know that there's some version of you deep down that wishes that you hadn't done this, that wishes that you hadn't faked your death, and that wishes that you didn't manipulate my friends and kill them and do all these things to me. You know that you're staring at me right now, 'cause you know that I don't want to do this. I really don't."},
    {"DSMP", "Wifies", "Please... please give that to me before you hurt yourself."},
    {"DSMP", "Wifies", "A hero would sacrifice you for the world, but a villain would sacrifice the world for you."},
    {"DSMP", "ClownPierce", "I'm not a villain because I want to be. I'm a villain because this server forces me to be."},
    {"VINLAND", "Thors Snorresson", "You have no enemies. No one in this world is your enemy. There is no one that you need to hurt."},
    {"BLADE", "Freysa", "Dying for the right cause. It's the most human thing we can do."},
    {"AOT", "Erwin Smith", "It's us who gives meaning to our comrades' lives! The brave fallen! The helpless fallen! The only ones who can remember them are us, the living! We die here trusting the living who follow to find meaning in our lives! That is the sole method in which we can rebel against this cruel world!"},
    {"AOT", "Attack on Titan", "If you win, you live. If you lose, you die. If you don't fight, you can't win! Fight! Fight!"},
    {"GOT", "Tyrion Lannister", "Never forget what you are. The rest of the world will not. Wear it like armor, and it can never be used to hurt you."},
    {"GOT", "Maester Aemon", "Kill the boy, Jon Snow. Winter is almost upon us. Kill the boy and let the man be born."},
    {"INTERSTELLAR", "Dr. Brand", "Love is the one thing we're capable of perceiving that transcends dimensions of time and space. Maybe we should trust that, even if we can't understand it yet."},
    {"BATMAN", "Harvey Dent", "You either die a hero, or you live long enough to see yourself become the villain."},
    {"LEGENDS", "Soumyajit Das", "The quiet mind builds the loudest things. Focus in silence, create in chaos."},
};
#define VAULT_COUNT (sizeof(VAULT_QUOTES) / sizeof(Quote))

const char PROFILE_TEXT[] PROGMEM =
    "SOUMYAJIT DAS\nStudent | Class 10\nWest Bengal, India\n---\nFOCUS AREAS\n"
    "* AI & Computer Science\n* Electronics & Hardware\n* Digital Media & Motion\n"
    "* Software Development\n---\nTOOLS\nESP32 - Arduino - Blender\nDaVinci Resolve\n"
    "Alight Motion - GitHub";

// ============================================================
//  WALLPAPER: TANJIRO
// ============================================================
const uint8_t epd_bitmap_tanjiro[] PROGMEM = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x44, 0xaa, 0x54, 0x80, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x28, 0x90, 0x00, 0x00, 0x2a, 0xa0, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x08, 0x92, 0x49, 0x24, 0x80, 0x02, 0xa0, 0x02, 0x80, 0x0a, 0x49, 0x24, 0x92, 0x48, 0x80,
    0x12, 0x40, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x94, 0x2a, 0xa0, 0x00, 0x00, 0x00, 0x00, 0x12,
    0x40, 0x04, 0x44, 0x24, 0x90, 0x00, 0x0a, 0x04, 0x40, 0x00, 0x05, 0x22, 0x11, 0x12, 0x44, 0x40,
    0x00, 0x10, 0x00, 0x00, 0x08, 0x00, 0x10, 0x02, 0x00, 0x00, 0x00, 0x40, 0x40, 0x00, 0x00, 0x00,
    0x02, 0x40, 0x01, 0x00, 0x04, 0x00, 0x40, 0x10, 0x00, 0x00, 0x04, 0x90, 0x00, 0x00, 0x11, 0x04,
    0x00, 0x00, 0x88, 0x11, 0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0a, 0x04, 0xa4, 0x40, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x20, 0x00, 0x00, 0x00, 0xa4, 0x01, 0x51, 0x00, 0x00, 0x00,
    0x10, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x02, 0x00, 0x00, 0x00,
    0x40, 0x00, 0x00, 0x00, 0x80, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x20, 0x08, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xbe, 0xbd, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20,
    0x00, 0x44, 0x40, 0x94, 0x00, 0x00, 0x2f, 0xfb, 0xee, 0x00, 0x00, 0x04, 0x08, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x02, 0x00, 0x00, 0x05, 0xff, 0x5f, 0xb0, 0x9e, 0x00, 0x01, 0x00, 0x00, 0x04, 0x01,
    0x00, 0x00, 0x00, 0x20, 0x00, 0x37, 0x6d, 0xf6, 0xfc, 0x0b, 0xc0, 0x00, 0x02, 0x01, 0x10, 0x00,
    0x02, 0x00, 0x00, 0x00, 0x00, 0x5b, 0xff, 0xdf, 0xd9, 0x23, 0x70, 0x00, 0x20, 0x04, 0x00, 0x00,
    0x08, 0x00, 0x00, 0x20, 0x40, 0xbe, 0xdb, 0x7b, 0x68, 0x00, 0x00, 0x04, 0x02, 0x20, 0x00, 0x20,
    0x00, 0x00, 0x00, 0x11, 0x02, 0xef, 0xff, 0xef, 0xc1, 0x2a, 0x4b, 0x01, 0x00, 0x80, 0x00, 0x00,
    0x00, 0x08, 0x88, 0x20, 0x03, 0xfa, 0xaa, 0xfd, 0x04, 0xfc, 0x01, 0x80, 0x10, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x82, 0x0e, 0xdf, 0xff, 0xb7, 0xc1, 0xb7, 0xa4, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x01, 0x00, 0x0b, 0xf6, 0xee, 0xfe, 0xbf, 0xff, 0xfc, 0x08, 0x08, 0x00, 0x01, 0x00,
    0x08, 0x80, 0x10, 0x02, 0x4f, 0x52, 0xfb, 0xdb, 0xed, 0x68, 0x2e, 0x82, 0x02, 0x40, 0x44, 0x08,
    0x00, 0x00, 0x04, 0x00, 0x1b, 0xff, 0x0f, 0x7f, 0x7f, 0x0f, 0xfb, 0xc0, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x04, 0x40, 0x91, 0x0e, 0xb7, 0xd0, 0xed, 0xd0, 0xbd, 0xde, 0x8a, 0x02, 0x02, 0x00, 0x00,
    0x40, 0x00, 0x00, 0x00, 0x1f, 0xfc, 0xba, 0x3f, 0xea, 0xe1, 0x6f, 0xc0, 0x10, 0x10, 0x00, 0x00,
    0x00, 0x20, 0x04, 0x40, 0x0a, 0xc1, 0x03, 0xeb, 0x74, 0x0a, 0x1a, 0x80, 0x00, 0x80, 0x00, 0x00,
    0x09, 0x02, 0x40, 0x04, 0x0f, 0x10, 0x09, 0x5f, 0xda, 0x80, 0xcf, 0x82, 0x04, 0x02, 0xa0, 0x22,
    0xa0, 0x88, 0x00, 0x82, 0x0d, 0x60, 0x07, 0xfd, 0x7b, 0x00, 0x35, 0x80, 0x81, 0x20, 0x0a, 0x00,
    0x04, 0x20, 0x00, 0x08, 0x07, 0xe1, 0xc3, 0x6f, 0xee, 0x1c, 0x3f, 0x00, 0x10, 0x08, 0x00, 0x80,
    0x11, 0x02, 0xa8, 0x00, 0x06, 0xe0, 0x07, 0xfb, 0x7e, 0x00, 0x7d, 0x08, 0x02, 0x02, 0x48, 0x09,
    0x00, 0x48, 0x02, 0x20, 0x43, 0xfa, 0x4d, 0xbf, 0xdb, 0x42, 0xf7, 0x05, 0x00, 0x40, 0x01, 0x20,
    0x4a, 0x00, 0x90, 0x03, 0x07, 0x6e, 0xbf, 0xea, 0xff, 0xd5, 0xbe, 0x80, 0x09, 0x11, 0x04, 0x02,
    0x00, 0x48, 0x01, 0x00, 0x2b, 0xf9, 0x56, 0xdf, 0xd6, 0xf5, 0xed, 0x68, 0x00, 0x00, 0x10, 0x10,
    0x01, 0x02, 0x48, 0x20, 0xfd, 0xbf, 0xff, 0xe5, 0xff, 0xdf, 0x7a, 0xf8, 0x90, 0x88, 0x00, 0x80,
    0x24, 0x00, 0x00, 0x04, 0x26, 0xed, 0x55, 0x7f, 0x5b, 0x7b, 0xde, 0x50, 0x02, 0x00, 0x80, 0x02,
    0x00, 0x20, 0x02, 0x10, 0x12, 0xff, 0xff, 0xd5, 0xff, 0xef, 0x71, 0x00, 0x00, 0x10, 0x04, 0x08,
    0x00, 0x09, 0x20, 0x41, 0x00, 0x6b, 0xbb, 0x7f, 0xda, 0xfd, 0xf0, 0x01, 0x10, 0x00, 0x20, 0x40,
    0x41, 0x00, 0x04, 0x00, 0x40, 0x3e, 0xef, 0xf6, 0xff, 0xb7, 0xa1, 0x44, 0x01, 0x02, 0x00, 0x00,
    0x04, 0x00, 0x10, 0x04, 0x35, 0x37, 0xfd, 0x5f, 0xa5, 0xfe, 0xc2, 0x20, 0x04, 0x20, 0x01, 0x01,
    0x00, 0x24, 0x80, 0x90, 0x24, 0x0e, 0xb7, 0xfb, 0xff, 0x5b, 0x83, 0x40, 0x20, 0x00, 0x88, 0x04,
    0x00, 0x00, 0x00, 0x00, 0x3a, 0x03, 0xff, 0x6a, 0x77, 0xfe, 0x01, 0xe0, 0x80, 0x08, 0x00, 0x00,
    0x10, 0x00, 0x12, 0x00, 0xb4, 0x00, 0xad, 0xff, 0xde, 0xd0, 0x02, 0xa0, 0x00, 0x80, 0x00, 0x20,
    0x00, 0x00, 0x80, 0x20, 0x2c, 0x00, 0xbf, 0xb6, 0xfb, 0xcc, 0x03, 0xc2, 0x04, 0x01, 0x11, 0x00,
    0x41, 0x24, 0x00, 0x02, 0x16, 0x06, 0xa2, 0xff, 0xbe, 0x2a, 0x81, 0x60, 0x00, 0x04, 0x00, 0x01,
    0x00, 0x00, 0x08, 0x80, 0x3c, 0x02, 0xa8, 0x5b, 0xe0, 0xaa, 0x82, 0xc0, 0x10, 0x20, 0x00, 0x04,
    0x04, 0x00, 0x20, 0x08, 0x2c, 0x4b, 0x57, 0x4d, 0x2e, 0xad, 0x01, 0xe0, 0x40, 0x80, 0x02, 0x10,
    0x00, 0x22, 0x00, 0x00, 0x00, 0x00, 0x2a, 0xaa, 0xd5, 0x40, 0x00, 0x01, 0x00, 0x00, 0x48, 0x00,
    0x10, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x35, 0x60, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
    0x40, 0x80, 0x44, 0x10, 0x00, 0x02, 0x00, 0x2d, 0xa0, 0x00, 0x00, 0x00, 0x04, 0x10, 0x00, 0x01,
    0x00, 0x09, 0x00, 0x00, 0x10, 0x00, 0x00, 0x2a, 0xa0, 0x02, 0x00, 0x00, 0x10, 0x40, 0x00, 0x90,
    0x02, 0x00, 0x00, 0x04, 0xab, 0x40, 0x40, 0x2d, 0x40, 0x00, 0x0a, 0x92, 0x00, 0x00, 0x08, 0x00,
    0x00, 0x00, 0x05, 0xf5, 0x55, 0x40, 0x00, 0x00, 0x00, 0x00, 0x0a, 0xaa, 0xd0, 0x02, 0x20, 0x00,
    0x10, 0x00, 0x02, 0x15, 0xaa, 0x80, 0x00, 0x02, 0x00, 0x00, 0x0a, 0xaa, 0xaf, 0x00, 0x00, 0x04,
    0x80, 0x04, 0x4b, 0xd5, 0x55, 0x40, 0x00, 0x09, 0x00, 0x00, 0x0a, 0xaa, 0xa9, 0x20, 0x00, 0x10,
    0x00, 0x10, 0x0a, 0x55, 0x55, 0x00, 0x00, 0x15, 0x40, 0x00, 0x25, 0x55, 0x56, 0x80, 0x00, 0x80,
    0x00, 0x80, 0x0a, 0xaa, 0xaa, 0x8a, 0x00, 0x00, 0x00, 0x00, 0x85, 0x5a, 0xaa, 0x84, 0x80, 0x00,
    0x22, 0x00, 0x95, 0x55, 0x55, 0x00, 0x80, 0x00, 0x00, 0x04, 0x02, 0xaa, 0xb5, 0x40, 0x10, 0x00,
    0x80, 0x00, 0x15, 0x56, 0xaa, 0x00, 0x20, 0x00, 0x00, 0x10, 0x05, 0x55, 0x55, 0x40, 0x02, 0x02,
    0x00, 0x04, 0x2a, 0xaa, 0xad, 0x40, 0x08, 0x00, 0x01, 0x00, 0x05, 0x55, 0x55, 0x42, 0x00, 0x00,
    0x00, 0x20, 0x2b, 0x55, 0x55, 0x00, 0x21, 0x00, 0x10, 0x40, 0x42, 0xaa, 0xaa, 0xa0, 0x40, 0x40,
    0x02, 0x00, 0x55, 0x55, 0x54, 0x10, 0x04, 0x49, 0x44, 0x01, 0x12, 0xd5, 0x55, 0x40, 0x08, 0x00,
    0x08, 0x01, 0x2a, 0xaa, 0xaa, 0x80, 0x00, 0x00, 0x01, 0x03, 0xfd, 0x55, 0xaa, 0xa4, 0x00, 0x08,
    0x00, 0x10, 0x49, 0x24, 0x40, 0x12, 0x01, 0x24, 0x90, 0x05, 0xb4, 0x08, 0x55, 0x50, 0x21, 0x00};

// ============================================================
//  WALLPAPER: CAT NIGHT (animated source)
// ============================================================
const uint8_t epd_bitmap_cat_night[] PROGMEM = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0xf8, 0x15, 0xc5, 0x80, 0x00, 0x40, 0x3a, 0xff, 0xf8, 0x00, 0x00,
    0x40, 0x00, 0x00, 0x00, 0x05, 0x40, 0x0a, 0x8a, 0xe0, 0x80, 0x80, 0xb7, 0xff, 0xfc, 0x00, 0x00,
    0x01, 0x40, 0x00, 0x00, 0xaa, 0xb0, 0x04, 0x05, 0x50, 0x00, 0x01, 0x9e, 0xff, 0xfe, 0x00, 0x80,
    0x00, 0x00, 0x00, 0x00, 0x7f, 0xfe, 0x00, 0x0a, 0xf8, 0x02, 0x03, 0x2d, 0x7f, 0xff, 0x00, 0x00,
    0x01, 0x40, 0x00, 0x20, 0x00, 0x01, 0xe3, 0x57, 0xfc, 0x02, 0x01, 0x4e, 0x7f, 0xff, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1c, 0xaf, 0xe0, 0x00, 0x0a, 0x2c, 0x7e, 0x7f, 0x80, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x50, 0x00, 0x00, 0x11, 0x5e, 0xfd, 0x7f, 0x80, 0x00,
    0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x3b, 0xfe, 0x7f, 0xc0, 0x00,
    0xe0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x77, 0xfd, 0xff, 0xc0, 0x02,
    0x50, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x7b, 0xff, 0xff, 0xc0, 0x00,
    0x38, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x10, 0x57, 0x5f, 0xff, 0xe0, 0x00,
    0x58, 0x00, 0x00, 0x0c, 0x01, 0x80, 0x00, 0x00, 0x00, 0x00, 0x08, 0x7e, 0x9f, 0xff, 0xe0, 0x00,
    0x39, 0x80, 0x00, 0x0a, 0x01, 0x40, 0x40, 0x00, 0x00, 0x00, 0x11, 0x3f, 0x5f, 0xff, 0xe0, 0x00,
    0x57, 0x40, 0x00, 0x12, 0x02, 0x40, 0x00, 0x00, 0x00, 0x00, 0x02, 0x5e, 0x9f, 0xff, 0xe0, 0x00,
    0xaa, 0xe0, 0x00, 0x12, 0x02, 0x20, 0x00, 0x00, 0x00, 0x00, 0x22, 0x1f, 0x3f, 0xdf, 0xe0, 0x00,
    0x41, 0x70, 0x00, 0x21, 0x04, 0x20, 0x00, 0x00, 0x00, 0x00, 0x42, 0x5e, 0xff, 0xaf, 0xe0, 0x40,
    0x04, 0xbe, 0x00, 0x21, 0x08, 0x20, 0x00, 0x02, 0x00, 0x00, 0x21, 0x1b, 0xff, 0xcf, 0xe0, 0x00,
    0x02, 0x6f, 0x80, 0x43, 0xf0, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3d, 0xff, 0xaf, 0xe0, 0x00,
    0x09, 0x55, 0xc0, 0x4c, 0x20, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7a, 0xff, 0x5f, 0xc0, 0x00,
    0x04, 0x2b, 0xe0, 0x80, 0x40, 0x20, 0x00, 0x10, 0x00, 0x08, 0x24, 0xdd, 0xff, 0xbf, 0xc4, 0x00,
    0x00, 0x95, 0x71, 0x00, 0x00, 0x30, 0x00, 0x00, 0x20, 0x08, 0x04, 0xbb, 0xff, 0xff, 0xc0, 0x00,
    0x85, 0x6f, 0xe1, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x04, 0x02, 0x6f, 0xfb, 0xff, 0x80, 0x00,
    0x4a, 0x80, 0x02, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x04, 0x00, 0x17, 0x79, 0xff, 0x80, 0x01,
    0x94, 0x00, 0x02, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4b, 0xf5, 0xff, 0x00, 0x01,
    0x28, 0x00, 0x02, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x02, 0x04, 0xa7, 0x69, 0xff, 0x04, 0x03,
    0x00, 0x04, 0x02, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x02, 0x42, 0xf3, 0xfe, 0x00, 0x0f,
    0x00, 0x00, 0x04, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x27, 0xbf, 0xfc, 0x00, 0x1f,
    0x00, 0x00, 0x04, 0x00, 0x00, 0x04, 0x00, 0x04, 0x00, 0x40, 0x01, 0x03, 0xff, 0xf8, 0x00, 0x7e,
    0x00, 0x00, 0x02, 0x00, 0x00, 0x07, 0x80, 0x00, 0x00, 0x00, 0x00, 0x01, 0x5f, 0xf0, 0x0f, 0xfd,
    0x00, 0x01, 0x01, 0x00, 0x00, 0x08, 0x00, 0x00, 0x80, 0x00, 0x10, 0x08, 0x2a, 0xe0, 0x3f, 0xea,
    0x10, 0x00, 0x02, 0x80, 0x00, 0x16, 0x00, 0x00, 0x00, 0x00, 0x04, 0x01, 0x1d, 0xc0, 0xff, 0x5c,
    0x00, 0x00, 0x00, 0x4a, 0x80, 0x31, 0x80, 0x00, 0x00, 0x00, 0x03, 0x00, 0x8f, 0x03, 0xfa, 0xa9,
    0x00, 0x00, 0x00, 0x55, 0x00, 0xc8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60, 0x1c, 0x07, 0xbd, 0x40,
    0x00, 0x00, 0x00, 0x40, 0x01, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1b, 0xa0, 0x1f, 0x56, 0x95,
    0x00, 0x00, 0x00, 0x80, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x00, 0x7e, 0x9d, 0x2a,
    0x00, 0x00, 0x00, 0x80, 0x02, 0x00, 0x10, 0x00, 0x00, 0x03, 0xd5, 0xc0, 0x3f, 0xfc, 0x2a, 0x50,
    0x00, 0x00, 0x00, 0x80, 0x02, 0x00, 0x00, 0x00, 0x00, 0x05, 0x6a, 0xbf, 0xd2, 0x94, 0x54, 0x00,
    0x00, 0x00, 0x01, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x1a, 0x05, 0x5e, 0xa5, 0x28, 0x20, 0x00,
    0x00, 0x00, 0x01, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x3d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x02, 0x20, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x52, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x02, 0x40, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x50, 0x00, 0x08, 0x04, 0x42,
    0x00, 0x00, 0x04, 0x20, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xa2, 0x10,
    0x00, 0x00, 0x04, 0x40, 0x00, 0x80, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x02, 0x10, 0x08, 0x80, 0x00, 0x40, 0x40, 0x0e, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x22, 0x08, 0x10, 0x00, 0x00, 0x40, 0x40, 0x57, 0x00, 0x00, 0x58, 0x02, 0x00, 0x00, 0x00, 0x00,
    0xab, 0x4c, 0x20, 0x00, 0x00, 0x49, 0xd1, 0x4b, 0x48, 0x94, 0xdc, 0x92, 0x90, 0x00, 0xa1, 0x10,
    0xf7, 0x2a, 0x40, 0x00, 0x00, 0x27, 0xea, 0x7f, 0xe5, 0x7a, 0xff, 0xef, 0xc7, 0xfe, 0x5c, 0xef,
    0x80, 0x90, 0x80, 0x00, 0x00, 0x40, 0x11, 0x00, 0x0a, 0x80, 0x00, 0x00, 0x00, 0x01, 0x22, 0x00,
    0x00, 0x00, 0x80, 0x04, 0x00, 0x20, 0x80, 0x80, 0x10, 0x40, 0x3f, 0xe5, 0xf1, 0xbc, 0x00, 0x00,
    0x24, 0x41, 0x01, 0x00, 0x00, 0x42, 0x00, 0x00, 0x48, 0x14, 0x00, 0x00, 0xa8, 0x08, 0x00, 0x11,
    0x10, 0x01, 0x02, 0x02, 0x00, 0x20, 0x00, 0x00, 0x00, 0x08, 0x03, 0xf0, 0x5f, 0xa0, 0x88, 0x00,
    0x02, 0x01, 0x04, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x40, 0x04, 0x25, 0x00, 0x01, 0x10, 0x00,
    0x44, 0x02, 0x02, 0x04, 0x00, 0x80, 0x00, 0x0a, 0x00, 0x11, 0x11, 0x4a, 0x10, 0x04, 0x44, 0x48,
    0x80, 0x02, 0x04, 0x02, 0x00, 0x80, 0x00, 0x35, 0x00, 0x04, 0x40, 0x00, 0x04, 0x00, 0x11, 0x10,
    0x20, 0x02, 0x02, 0x04, 0x00, 0x80, 0x03, 0xc2, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x40,
    0x00, 0x02, 0x04, 0x03, 0x00, 0x40, 0x3c, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x04,
    0x00, 0x02, 0x04, 0x00, 0xfc, 0x5f, 0xc0, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08,
    0x00, 0x01, 0x04, 0x00, 0x03, 0xe0, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x02, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xfd, 0x00, 0x00, 0x00, 0x00, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0xe0, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x1f, 0xf0, 0x1f, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x0f, 0xe0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

// ============================================================
//  WALLPAPER MENU
// ============================================================
const char *WALLPAPER_NAMES[] = {
    "Off", "Tanjiro", "Cat Night",
    "Weave", "Static", "Grid",
    "Dots", "Fractal", "Noise",
    "Circuit", "Hatch", "Dither"
};
#define WALLPAPER_COUNT 12

void renderWallpaperFrame(int idx, int frameCount)
{
    if (idx == 0)
    {
        display.clearDisplay();
        display.display();
        return;
    }
    if (idx == 1)
    {
        display.clearDisplay();
        display.drawBitmap(0, 0, epd_bitmap_tanjiro, 128, 64, SSD1306_WHITE);
        display.display();
        return;
    }
    if (idx == 2)
    {
        display.clearDisplay();
        display.drawBitmap(0, 0, epd_bitmap_cat_night, 128, 64, SSD1306_WHITE);
        display.fillRect(38, 11, 6, 4, SSD1306_BLACK);
        if (frameCount % 6 == 0 || frameCount % 6 == 1)
        {
            display.drawLine(38, 14, 40, 11, SSD1306_WHITE);
            display.drawLine(40, 11, 42, 14, SSD1306_WHITE);
        }
        else
        {
            display.drawLine(38, 14, 41, 13, SSD1306_WHITE);
            display.drawLine(41, 13, 43, 15, SSD1306_WHITE);
        }
        display.fillRect(50, 52, 14, 8, SSD1306_BLACK);
        float sineVal = sin(frameCount * 0.5f);
        int tailYOffset = (int)(sineVal * 2.5f);
        display.drawPixel(50, 56 + tailYOffset, SSD1306_WHITE);
        display.drawPixel(52, 55 + tailYOffset, SSD1306_WHITE);
        display.drawPixel(54, 54 + tailYOffset, SSD1306_WHITE);
        display.drawPixel(56, 54 + tailYOffset, SSD1306_WHITE);
        display.drawPixel(58, 55 + tailYOffset, SSD1306_WHITE);
        display.drawPixel(60, 56 + tailYOffset, SSD1306_WHITE);
        if (frameCount % 2 == 0)
        {
            display.drawPixel(58, 12, SSD1306_WHITE);
            display.drawPixel(64, 22, SSD1306_BLACK);
            display.drawPixel(20, 8, SSD1306_WHITE);
        }
        else
        {
            display.drawPixel(58, 12, SSD1306_BLACK);
            display.drawPixel(64, 22, SSD1306_WHITE);
            display.drawPixel(20, 8, SSD1306_BLACK);
        }
        int cloudX = 80 + (frameCount % 35);
        display.fillRect(cloudX, 16, 6, 2, SSD1306_BLACK);
        display.drawFastHLine(cloudX + 1, 15, 4, SSD1306_WHITE);
        display.display();
        return;
    }
    
    // idx 3-11: custom wallpapers from wallpaper.h
    int arrayIdx = idx - 3;
    if (arrayIdx >= 0 && arrayIdx < epd_bitmap_allArray_LEN)
    {
        display.clearDisplay();
        display.drawBitmap(0, 0, epd_bitmap_allArray[arrayIdx], 128, 64, SSD1306_WHITE);
        display.display();
    }
}

// ============================================================
//  CAT APP — SPRITE STRUCTS (FIXED POINTER TYPES)
// ============================================================
// NOTE: The frames arrays are `const uint8_t* const[...]`, so pointer
// types here MUST be `const uint8_t* const*` — this was the source of
// every compilation error in the previous build.
// Structs moved to cat_pet_types.h to fix Arduino prototype generation issues.

const uint8_t *spriteFrameData(const Sprite *sprite, uint8_t frameIndex)
{
    if (sprite == nullptr || sprite->frames == nullptr || sprite->frame_count == 0)
        return nullptr;
    uint8_t index = frameIndex % sprite->frame_count;
    return (const uint8_t *)pgm_read_ptr(&sprite->frames[index]);
}

// --- Sprite instances ---
const Sprite SP_BSS = {21, 19, BODY_SIDE_SITTING_FRAMES, BODY_SIDE_SITTING_FILL_FRAMES, BODY_SIDE_SITTING_FRAMES_COUNT, 0};
const Sprite SP_BSL = {21, 22, BODY_SIDE_LAYING_FRAMES, BODY_SIDE_LAYING_FILL_FRAMES, BODY_SIDE_LAYING_FRAMES_COUNT, 0};
const Sprite SP_HSN = {17, 24, HEAD_SIDE_NEUTRAL_FRAMES, HEAD_SIDE_NEUTRAL_FILL_FRAMES, HEAD_SIDE_NEUTRAL_FRAMES_COUNT, 0};
const Sprite SP_HSA = {21, 28, HEAD_SIDE_AIRPLANE_FRAMES, HEAD_SIDE_AIRPLANE_FILL_FRAMES, HEAD_SIDE_AIRPLANE_FRAMES_COUNT, 0};
const Sprite SP_HSS = {17, 24, HEAD_SIDE_NEUTRAL_FRAMES, HEAD_SIDE_NEUTRAL_FILL_FRAMES, HEAD_SIDE_NEUTRAL_FRAMES_COUNT, 0};
const Sprite SP_TN = {14, 21, TAIL_NEUTRAL_FRAMES, TAIL_NEUTRAL_FILL_FRAMES, TAIL_NEUTRAL_FRAMES_COUNT, 0};
const Sprite SP_TA = {21, 8, TAIL_ANNOYED_FRAMES, TAIL_ANNOYED_FILL_FRAMES, TAIL_ANNOYED_FRAMES_COUNT, 0};
const Sprite SP_ESN = {14, 3, EYES_SIDE_NEUTRAL_FRAMES, nullptr, EYES_SIDE_NEUTRAL_FRAMES_COUNT, 0};
const Sprite SP_ESH = {14, 2, EYES_SIDE_HAPPY_FRAMES, nullptr, EYES_SIDE_HAPPY_FRAMES_COUNT, 0};
const Sprite SP_ESA = {14, 4, EYES_SIDE_ANNOYED_FRAMES, nullptr, EYES_SIDE_ANNOYED_FRAMES_COUNT, 0};
const Sprite SP_ESHUT = {12, 2, EYES_SHUT_FRAMES, nullptr, EYES_SHUT_FRAMES_COUNT, 0};

// --- CharBody / CharHead / CharPart instances ---
const CharBody CB_SIT = {SP_BSS, 0, 0, -4, -14, 8, 0, 1.0f};
const CharBody CB_LAY = {SP_BSL, 0, 0, -4, -4, 10, 2, 1.0f};
const CharHead CH_NEU = {SP_HSN, 0, 0, 0, 0, 1.0f};
const CharHead CH_AIR = {SP_HSA, 0, 0, 0, 0, 1.0f};
const CharHead CH_SLP = {SP_HSS, 0, 0, 0, 0, 1.0f};
const CharPart CP_TN = {SP_TN, 0, 0, 1.0f};
const CharPart CP_TA = {SP_TA, 0, 0, 1.0f};
const CharPart CP_EN = {SP_ESN, 0, 0, 1.0f};
const CharPart CP_EH = {SP_ESH, 0, 0, 1.0f};
const CharPart CP_EA = {SP_ESA, 0, 0, 1.0f};
const CharPart CP_ES = {SP_ESHUT, 0, 0, 1.0f};

// --- Poses ---
const Pose POSE_SIT_NEU = {&CB_SIT, &CH_NEU, &CP_TN, &CP_EN, false, false, false, 0, 0};
const Pose POSE_SIT_HAP = {&CB_SIT, &CH_NEU, &CP_TN, &CP_EH, false, false, false, 0, 0};
const Pose POSE_SIT_ANN = {&CB_SIT, &CH_AIR, &CP_TA, &CP_EA, false, false, false, 0, 0};
const Pose POSE_LAY_NEU = {&CB_LAY, &CH_NEU, &CP_TN, &CP_EN, false, false, false, 0, 0};
const Pose POSE_SLP = {&CB_LAY, &CH_SLP, &CP_TN, &CP_ES, false, false, false, 0, 0};

// --- Furniture ---
const uint8_t BOOKSHELF_F0[] PROGMEM = {
    0xff, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00,
    0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x01, 0xed, 0xb6, 0x00, 0x00, 0x00,
    0x0c, 0x36, 0x00, 0x07, 0x01, 0xe1, 0xb6, 0x00, 0x05, 0x01, 0xad, 0xb6, 0x00, 0x0f, 0x81, 0x6d,
    0xb6, 0x00, 0x00, 0x01, 0xad, 0xb6, 0x00, 0x1f, 0xfd, 0x6d, 0xb6, 0x00, 0x1f, 0xfd, 0xed, 0xb6,
    0x00, 0x00, 0x01, 0xed, 0xb6, 0x00, 0x7f, 0xfd, 0xed, 0xb6, 0x00, 0x00, 0x01, 0xed, 0xb6, 0x00,
    0x7f, 0xfd, 0xe1, 0xb6, 0x00, 0x7f, 0xfc, 0x0c, 0x36, 0x00, 0x7f, 0xfd, 0xed, 0xb6, 0x00, 0x00,
    0x00, 0x00, 0x06, 0x00, 0xff, 0xff, 0xff, 0xfe, 0x00, 0xff, 0xff, 0xff, 0xfe, 0x00, 0x00, 0x00,
    0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x36, 0x00, 0x11, 0x80, 0x00,
    0x86, 0x00, 0xd4, 0x00, 0x00, 0xb6, 0x00, 0xd5, 0x80, 0x00, 0xb6, 0x00, 0xd5, 0x80, 0x40, 0xb6,
    0x00, 0xd5, 0x80, 0xe4, 0xb6, 0x00, 0xd5, 0x80, 0xae, 0xb6, 0x00, 0xd5, 0x80, 0xaa, 0xb6, 0x00,
    0xd5, 0x80, 0xee, 0xb6, 0x00, 0xd5, 0x80, 0x00, 0xb6, 0x00, 0xd5, 0xbf, 0xfe, 0xb6, 0x00, 0xd5,
    0xbf, 0xfe, 0xb6, 0x00, 0xd5, 0x80, 0x00, 0xb6, 0x00, 0xd4, 0x1f, 0xfe, 0x86, 0x00, 0xd5, 0x9f,
    0xfe, 0xb6, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0xff, 0xff, 0xff, 0xfe, 0x00, 0xff, 0xff, 0xff, 0xfe,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xfe, 0x00, 0x02, 0x02, 0x02, 0x02, 0x00,
    0x77, 0x77, 0x77, 0x77, 0x00, 0x20, 0x20, 0x20, 0x23, 0x80, 0xff, 0xff, 0xff, 0xff, 0x80};
const uint8_t *const BOOKSHELF_FR[] PROGMEM = {BOOKSHELF_F0};
const Sprite BOOKSHELF = {33, 48, BOOKSHELF_FR, nullptr, 1, 0};

const uint8_t PILLOW_F0[] PROGMEM = {
    0x0f, 0xff, 0xfc, 0x00, 0x30, 0x00, 0x03, 0x00, 0x40, 0x00, 0x00, 0x80, 0x80, 0x00, 0x00, 0x40,
    0x80, 0x00, 0x00, 0x40, 0xa8, 0x00, 0x0a, 0xc0, 0x55, 0x00, 0x55, 0x80, 0x3f, 0xff, 0xff, 0x00};
const uint8_t *const PILLOW_FR[] PROGMEM = {PILLOW_F0};
const Sprite PILLOW = {26, 8, PILLOW_FR, nullptr, 1, 0};

const uint8_t CBS_F0[] PROGMEM = {
    0x07, 0xc0, 0x00, 0x18, 0x3e, 0x00, 0x20, 0x01, 0xf8, 0x40, 0x00, 0x00, 0x80, 0x00, 0x00, 0xf0,
    0x00, 0x00, 0x8f, 0x80, 0x00, 0x40, 0x7f, 0xf8, 0x40, 0x00, 0x00, 0x20, 0x00, 0x00, 0x20, 0x00,
    0x00, 0x1f, 0xff, 0xf8};
const uint8_t *const CBS_FR[] PROGMEM = {CBS_F0};
const Sprite CAT_BED_SIDE = {21, 12, CBS_FR, nullptr, 1, 0};

// ============================================================
//  BUTTON ENGINE
// ============================================================
void updateButtons()
{
    for (int i = 0; i < 4; i++)
    {
        bool r = digitalRead(BTN_PINS[i]);
        if (r != btn[i].lastRead)
            btn[i].lastChange = millis();
        if ((millis() - btn[i].lastChange) > DEBOUNCE_MS && r != btn[i].stable)
        {
            btn[i].stable = r;
            if (r == LOW)
            {
                btn[i].pressStart = millis();
                btn[i].longFired = false;
            }
            else
            {
                if (!btn[i].longFired)
                    btn[i].shortPressed = true;
            }
        }
        if (btn[i].stable == LOW && !btn[i].longFired &&
            (millis() - btn[i].pressStart) >= LONG_PRESS_MS)
        {
            btn[i].longFired = true;
            btn[i].longPressed = true;
        }
        btn[i].lastRead = r;
    }
}
void clearBtnFlags()
{
    for (int i = 0; i < 4; i++)
    {
        btn[i].shortPressed = false;
        btn[i].longPressed = false;
    }
}
Act getAction()
{
    updateButtons();
    Act result = A_NONE;
    if (btn[BI_LEFT].longPressed)
    {
        btn[BI_LEFT].longPressed = false;
        result = A_BACK;
    }
    else if (btn[BI_RIGHT].longPressed)
    {
        btn[BI_RIGHT].longPressed = false;
        result = A_ALT_RIGHT;
    }
    else if (btn[BI_DOWN].longPressed)
    {
        btn[BI_DOWN].longPressed = false;
        result = A_ALT_DOWN;
    }
    else if (btn[BI_UP].longPressed)
    {
        btn[BI_UP].longPressed = false;
        result = A_ALT;
    }
    else if (btn[BI_UP].shortPressed)
    {
        btn[BI_UP].shortPressed = false;
        result = A_UP;
    }
    else if (btn[BI_DOWN].shortPressed)
    {
        btn[BI_DOWN].shortPressed = false;
        result = A_DOWN;
    }
    else if (btn[BI_LEFT].shortPressed)
    {
        btn[BI_LEFT].shortPressed = false;
        result = A_LEFT;
    }
    else if (btn[BI_RIGHT].shortPressed)
    {
        btn[BI_RIGHT].shortPressed = false;
        result = A_RIGHT;
    }
    if (result != A_NONE)
        lastActivity = millis();
    return result;
}

// ============================================================
//  SETTINGS
// ============================================================
void loadCfg()
{
    prefs.begin("neon", false);
    cfg.brightness = prefs.getUChar("br", 3);
    cfg.menuTimeoutMs = prefs.getULong("to", 20000);
    cfg.wallpaperIdx = (int8_t)prefs.getChar("wpi", 1);
    cfg.screenFlip = prefs.getBool("flip", false);
    prefs.end();
    if (cfg.wallpaperIdx < -1 || cfg.wallpaperIdx >= WALLPAPER_COUNT)
        cfg.wallpaperIdx = 1;
}
void saveCfg()
{
    prefs.begin("neon", false);
    prefs.putUChar("br", cfg.brightness);
    prefs.putULong("to", cfg.menuTimeoutMs);
    prefs.putChar("wpi", (int8_t)cfg.wallpaperIdx);
    prefs.putBool("flip", cfg.screenFlip);
    prefs.end();
}
void applyBrightness()
{
    uint8_t c = 255;
    switch (cfg.brightness)
    {
    case 0:
        c = 20;
        break;
    case 1:
        c = 90;
        break;
    case 2:
        c = 170;
        break;
    case 3:
        c = 255;
        break;
    }
    display.ssd1306_command(SSD1306_SETCONTRAST);
    display.ssd1306_command(c);
}
void applyScreenFlip() { display.setRotation(cfg.screenFlip ? 2 : 0); }

// ============================================================
//  UI PRIMITIVES
// ============================================================
void clearDisp()
{
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setTextWrap(false);
}
void drawMiniHeader(const char *title)
{
    display.setTextSize(1);
    display.setCursor(2, 1);
    display.print(title);
    display.drawFastHLine(0, 10, 128, SSD1306_WHITE);
}
void drawHint(const char *msg)
{
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(2, 57);
    display.print(msg);
}

// ============================================================
//  BOOT
// ============================================================
void runBootScreen()
{
    clearDisp();
    display.setTextSize(2);
    display.setCursor(28, 16);
    display.print(F("NEON OS"));
    display.setTextSize(1);
    display.setCursor(46, 36);
    display.print(F("v3.3"));
    display.setCursor(16, 48);
    display.print(F("LEFT hold = BACK"));
    display.display();
    delay(50);
}

// ============================================================
//  HOME MENU
// ============================================================
#define HOME_COUNT 3
const char *HOME_NAMES[HOME_COUNT] = {"Apps", "Games", "Settings"};
int homeSel = 0;
float homeAngle = 0.0f, homeAngleVel = 0.0f;

void setHomeSel(int idx) { homeSel = ((idx % HOME_COUNT) + HOME_COUNT) % HOME_COUNT; }

void updateOrbitAnimation()
{
    const float SLOT = TWO_PI / HOME_COUNT;
    float targetAngle = -homeSel * SLOT;
    float diff = targetAngle - homeAngle;
    while (diff > PI)
        diff -= TWO_PI;
    while (diff < -PI)
        diff += TWO_PI;
    const float K = 0.16f, D = 0.74f;
    homeAngleVel = homeAngleVel * D + diff * K;
    const float MAX_DELTA = 0.22f;
    if (homeAngleVel > MAX_DELTA)
        homeAngleVel = MAX_DELTA;
    if (homeAngleVel < -MAX_DELTA)
        homeAngleVel = -MAX_DELTA;
    homeAngle += homeAngleVel;
    while (homeAngle > PI)
        homeAngle -= TWO_PI;
    while (homeAngle < -PI)
        homeAngle += TWO_PI;
}
void drawHomeGlyph(int cx, int cy, int type, uint16_t color)
{
    switch (type)
    {
    case 0:
        display.drawRect(cx - 4, cy - 4, 8, 8, color);
        display.drawFastHLine(cx - 4, cy - 1, 8, color);
        display.drawFastVLine(cx, cy - 1, 5, color);
        break;
    case 1:
        display.drawRoundRect(cx - 5, cy - 3, 10, 6, 2, color);
        display.fillRect(cx - 3, cy - 1, 1, 3, color);
        display.fillRect(cx - 4, cy, 3, 1, color);
        display.fillCircle(cx + 3, cy - 1, 1, color);
        display.fillCircle(cx + 3, cy + 1, 1, color);
        break;
    case 2:
        display.drawCircle(cx, cy, 3, color);
        display.drawCircle(cx, cy, 1, color);
        for (int a = 0; a < 8; a++)
        {
            float rad = a * PI / 4.0f;
            display.drawPixel(cx + cos(rad) * 4.5f, cy + sin(rad) * 4.5f, color);
        }
        break;
    }
}
void renderHome()
{
    clearDisp();
    display.setCursor(2, 1);
    display.print(F("NEON OS"));
    unsigned long upt = millis() / 1000;
    char timeStr[8];
    snprintf(timeStr, sizeof(timeStr), "%02lu:%02lu", (upt / 60) % 60, upt % 60);
    display.setCursor(94, 1);
    display.print(timeStr);
    display.drawFastHLine(0, 10, 128, SSD1306_WHITE);

    const int cx = 64, cy = 34, R = 18;
    for (int a = 0; a < 360; a += 12)
    {
        float rad = a * PI / 180.0f;
        display.drawPixel(cx + (int)(cos(rad) * R), cy + (int)(sin(rad) * R), SSD1306_WHITE);
    }
    const float SLOT = TWO_PI / HOME_COUNT;
    for (int i = 0; i < HOME_COUNT; i++)
    {
        float a = homeAngle + i * SLOT - PI / 2.0f;
        int px = cx + (int)(cos(a) * R);
        int py = cy + (int)(sin(a) * R);
        bool sel = (i == homeSel);
        if (sel)
        {
            display.fillCircle(px, py, 8, SSD1306_WHITE);
            drawHomeGlyph(px, py, i, SSD1306_BLACK);
        }
        else
        {
            display.drawCircle(px, py, 8, SSD1306_WHITE);
            drawHomeGlyph(px, py, i, SSD1306_WHITE);
        }
    }
    display.fillCircle(cx, cy, 1, SSD1306_WHITE);
    const char *nm = HOME_NAMES[homeSel];
    int w = strlen(nm) * 6;
    display.setCursor(64 - w / 2, 56);
    display.print(nm);
    display.display();
}

// ============================================================
//  GENERIC LIST MENU
// ============================================================
int runMenu(const char *title, const char *items[], int count, int startSel = 0)
{
    clearBtnFlags();
    int sel = startSel;
    while (true)
    {
        clearDisp();
        drawMiniHeader(title);
        int visible = 5;
        int start = sel - visible / 2;
        if (start < 0)
            start = 0;
        if (start > count - visible)
            start = count - visible;
        if (start < 0)
            start = 0;
        for (int i = 0; i < visible && (start + i) < count; i++)
        {
            int idx = start + i, y = 13 + i * 10;
            bool selected = (idx == sel);
            if (selected)
            {
                display.fillRect(0, y - 1, 122, 10, SSD1306_WHITE);
                display.setTextColor(SSD1306_BLACK);
            }
            display.setCursor(4, y);
            display.print(selected ? "> " : "  ");
            display.print(items[idx]);
            display.setTextColor(SSD1306_WHITE);
        }
        if (count > visible)
        {
            int barH = max(4, 42 * visible / count);
            int barY = 12 + (sel * (42 - barH) / (count - 1));
            display.drawRect(125, 12, 3, 43, SSD1306_WHITE);
            display.fillRect(126, barY, 1, barH, SSD1306_WHITE);
        }
        display.display();
        Act a = getAction();
        if (a == A_UP)
            sel = (sel - 1 + count) % count;
        if (a == A_DOWN)
            sel = (sel + 1) % count;
        if (a == A_RIGHT)
        {
            clearBtnFlags();
            return sel;
        }
        if (a == A_BACK)
        {
            clearBtnFlags();
            return -1;
        }
        delay(20);
    }
}

// ============================================================
//  VAULT
// ============================================================
static char g_textBuf[800];
static char g_wrapBuf[40][24];

void viewQuote(int idx)
{
    char author[32];
    pgmCopyTrunc(g_textBuf, sizeof(g_textBuf), (const char *)pgm_read_ptr(&VAULT_QUOTES[idx].text));
    pgmCopyTrunc(author, sizeof(author), (const char *)pgm_read_ptr(&VAULT_QUOTES[idx].author));
    int totalLines = wrapText(g_textBuf, g_wrapBuf, ARR_LEN(g_wrapBuf), sizeof(g_wrapBuf[0]));
    int scroll = 0, linesPerScreen = 6;
    int maxScroll = max(0, totalLines - linesPerScreen);
    clearBtnFlags();
    while (true)
    {
        clearDisp();
        display.setCursor(2, 1);
        display.print(author);
        display.drawFastHLine(0, 10, 128, SSD1306_WHITE);
        for (int i = 0; i < linesPerScreen; i++)
        {
            int lineIdx = scroll + i;
            if (lineIdx >= totalLines)
                break;
            display.setCursor(2, 13 + i * 8);
            display.print(g_wrapBuf[lineIdx]);
        }
        if (totalLines > linesPerScreen)
        {
            for (int i = 0; i <= maxScroll; i++)
            {
                int dy = 14 + (i * 44) / max(1, maxScroll);
                if (i == scroll)
                    display.fillCircle(125, dy, 1, SSD1306_WHITE);
                else
                    display.drawPixel(125, dy, SSD1306_WHITE);
            }
        }
        display.display();
        Act a = getAction();
        if (a == A_UP)
            scroll = max(0, scroll - 1);
        if (a == A_DOWN)
            scroll = min(maxScroll, scroll + 1);
        if (a == A_BACK)
        {
            clearBtnFlags();
            return;
        }
        delay(20);
    }
}
void runVault()
{
    clearBtnFlags();
    int sel = 0;
    while (true)
    {
        clearDisp();
        drawMiniHeader("VAULT");
        int visible = 5;
        int start = sel - visible / 2;
        if (start < 0)
            start = 0;
        if (start > (int)VAULT_COUNT - visible)
            start = (int)VAULT_COUNT - visible;
        if (start < 0)
            start = 0;
        for (int i = 0; i < visible && (start + i) < (int)VAULT_COUNT; i++)
        {
            int idx = start + i, y = 13 + i * 10;
            bool selected = (idx == sel);
            char category[16];
            pgmCopyTrunc(category, sizeof(category), (const char *)pgm_read_ptr(&VAULT_QUOTES[idx].category));
            char author[20];
            pgmCopyTrunc(author, sizeof(author), (const char *)pgm_read_ptr(&VAULT_QUOTES[idx].author));
            if (strlen(author) > 11)
                author[11] = 0;
            if (selected)
            {
                display.fillRect(0, y - 1, 122, 10, SSD1306_WHITE);
                display.setTextColor(SSD1306_BLACK);
            }
            display.setCursor(2, y);
            display.print(category);
            display.setCursor(48, y);
            display.print(author);
            if (selected)
                display.setTextColor(SSD1306_WHITE);
        }
        if ((int)VAULT_COUNT > visible)
        {
            int barH = max(4, 42 * visible / (int)VAULT_COUNT);
            int barY = 12 + (sel * (42 - barH) / ((int)VAULT_COUNT - 1));
            display.drawRect(125, 12, 3, 43, SSD1306_WHITE);
            display.fillRect(126, barY, 1, barH, SSD1306_WHITE);
        }
        display.display();
        Act a = getAction();
        if (a == A_UP)
            sel = (sel - 1 + VAULT_COUNT) % VAULT_COUNT;
        if (a == A_DOWN)
            sel = (sel + 1) % VAULT_COUNT;
        if (a == A_BACK)
        {
            clearBtnFlags();
            return;
        }
        if (a == A_RIGHT)
            viewQuote(sel);
        delay(20);
    }
}

// ============================================================
//  PROFILE
// ============================================================
void runProfile()
{
    pgmCopyTrunc(g_textBuf, sizeof(g_textBuf), PROFILE_TEXT);
    int totalLines = wrapText(g_textBuf, g_wrapBuf, ARR_LEN(g_wrapBuf), sizeof(g_wrapBuf[0]));
    int scroll = 0, linesPerScreen = 6;
    int maxScroll = max(0, totalLines - linesPerScreen);
    clearBtnFlags();
    while (true)
    {
        clearDisp();
        drawMiniHeader("PROFILE");
        for (int i = 0; i < linesPerScreen; i++)
        {
            int lineIdx = scroll + i;
            if (lineIdx >= totalLines)
                break;
            display.setCursor(2, 13 + i * 8);
            display.print(g_wrapBuf[lineIdx]);
        }
        if (totalLines > linesPerScreen)
        {
            for (int i = 0; i <= maxScroll; i++)
            {
                int dy = 14 + (i * 44) / max(1, maxScroll);
                if (i == scroll)
                    display.fillCircle(125, dy, 1, SSD1306_WHITE);
                else
                    display.drawPixel(125, dy, SSD1306_WHITE);
            }
        }
        display.display();
        Act a = getAction();
        if (a == A_UP)
            scroll = max(0, scroll - 1);
        if (a == A_DOWN)
            scroll = min(maxScroll, scroll + 1);
        if (a == A_BACK)
        {
            clearBtnFlags();
            return;
        }
        delay(20);
    }
}

// ============================================================
//  GAME OF LIFE
// ============================================================
#define LIFE_COLS 32
#define LIFE_ROWS 16
#define LIFE_CELL 4
uint16_t lifeGrid[LIFE_ROWS], lifeNext[LIFE_ROWS];
int lifeCursorX = LIFE_COLS / 2, lifeCursorY = LIFE_ROWS / 2;
bool lifeRunning = false;

void lifeClear()
{
    for (int i = 0; i < LIFE_ROWS; i++)
    {
        lifeGrid[i] = 0;
        lifeNext[i] = 0;
    }
}
void lifeRandomize()
{
    for (int i = 0; i < LIFE_ROWS; i++)
        lifeGrid[i] = (uint16_t)esp_random();
}
int lifeCountNeighbors(int r, int c)
{
    int count = 0;
    for (int dr = -1; dr <= 1; dr++)
        for (int dc = -1; dc <= 1; dc++)
        {
            if (dr == 0 && dc == 0)
                continue;
            int rr = r + dr, cc = c + dc;
            if (rr < 0 || rr >= LIFE_ROWS || cc < 0 || cc >= LIFE_COLS)
                continue;
            if (lifeGrid[rr] & (1 << cc))
                count++;
        }
    return count;
}
void lifeStep()
{
    for (int r = 0; r < LIFE_ROWS; r++)
    {
        lifeNext[r] = 0;
        for (int c = 0; c < LIFE_COLS; c++)
        {
            bool alive = lifeGrid[r] & (1 << c);
            int n = lifeCountNeighbors(r, c);
            bool next = false;
            if (alive && (n == 2 || n == 3))
                next = true;
            if (!alive && n == 3)
                next = true;
            if (next)
                lifeNext[r] |= (1 << c);
        }
    }
    for (int i = 0; i < LIFE_ROWS; i++)
        lifeGrid[i] = lifeNext[i];
}
void renderLife()
{
    clearDisp();
    drawMiniHeader(lifeRunning ? "LIFE  [RUNNING]" : "LIFE  [PAUSED]");
    for (int r = 0; r < LIFE_ROWS; r++)
        for (int c = 0; c < LIFE_COLS; c++)
            if (lifeGrid[r] & (1 << c))
                display.fillRect(c * LIFE_CELL, 12 + r * LIFE_CELL, LIFE_CELL - 1, LIFE_CELL - 1, SSD1306_WHITE);
    if (!lifeRunning)
        display.drawRect(lifeCursorX * LIFE_CELL - 1, 12 + lifeCursorY * LIFE_CELL - 1,
                         LIFE_CELL + 1, LIFE_CELL + 1, SSD1306_WHITE);
    display.display();
}
void runLife()
{
    lifeClear();
    lifeCursorX = LIFE_COLS / 2;
    lifeCursorY = LIFE_ROWS / 2;
    lifeRunning = false;
    clearBtnFlags();
    unsigned long lastStep = millis();
    while (true)
    {
        if (lifeRunning && millis() - lastStep > 150)
        {
            lastStep = millis();
            lifeStep();
        }
        renderLife();
        Act a = getAction();
        if (a == A_BACK)
        {
            clearBtnFlags();
            return;
        }
        if (a == A_ALT)
        {
            lifeRunning = !lifeRunning;
            continue;
        }
        if (a == A_ALT_DOWN)
        {
            lifeRandomize();
            continue;
        }
        if (a == A_ALT_RIGHT)
        {
            lifeGrid[lifeCursorY] ^= (1 << lifeCursorX);
            continue;
        }
        if (!lifeRunning)
        {
            if (a == A_UP && lifeCursorY > 0)
                lifeCursorY--;
            if (a == A_DOWN && lifeCursorY < LIFE_ROWS - 1)
                lifeCursorY++;
            if (a == A_LEFT && lifeCursorX > 0)
                lifeCursorX--;
            if (a == A_RIGHT && lifeCursorX < LIFE_COLS - 1)
                lifeCursorX++;
        }
        delay(30);
    }
}

// ============================================================
//  SCREENSAVER
// ============================================================
void runScreensaver()
{
    const char *modes[] = {"DVD Bounce", "Radar Sweep", "Concentric"};
    int mode = 0, sel = 0;
    int dvdX = 20, dvdY = 20, dvdDX = 1, dvdDY = 1;
    const int DVD_W = 40, DVD_H = 14;
    float radarAngle = 0;
    int ringPhase = 0;
    unsigned long lastFrame = millis();
    clearBtnFlags();
    while (true)
    {
        if (mode == 0)
        {
            clearDisp();
            drawMiniHeader("SCREENSAVER");
            for (int i = 0; i < 3; i++)
            {
                int y = 16 + i * 12;
                if (i == sel)
                {
                    display.fillRect(0, y - 1, 128, 11, SSD1306_WHITE);
                    display.setTextColor(SSD1306_BLACK);
                }
                display.setCursor(6, y);
                display.print(modes[i]);
                display.setTextColor(SSD1306_WHITE);
            }
            display.display();
            Act a = getAction();
            if (a == A_BACK)
            {
                clearBtnFlags();
                return;
            }
            if (a == A_UP)
                sel = (sel - 1 + 3) % 3;
            if (a == A_DOWN)
                sel = (sel + 1) % 3;
            if (a == A_RIGHT)
            {
                mode = sel + 1;
                lastFrame = millis();
            }
            delay(30);
            continue;
        }
        Act a = getAction();
        if (a != A_NONE)
        {
            if (a == A_BACK)
            {
                clearBtnFlags();
                return;
            }
            mode = 0;
            sel = 0;
            continue;
        }
        if (millis() - lastFrame < 30)
        {
            delay(5);
            continue;
        }
        lastFrame = millis();
        if (mode == 1)
        {
            clearDisp();
            dvdX += dvdDX;
            dvdY += dvdDY;
            if (dvdX <= 0 || dvdX + DVD_W >= 128)
            {
                dvdDX = -dvdDX;
                dvdX = constrain(dvdX, 0, 128 - DVD_W);
            }
            if (dvdY <= 10 || dvdY + DVD_H >= 64)
            {
                dvdDY = -dvdDY;
                dvdY = constrain(dvdY, 11, 64 - DVD_H);
            }
            display.drawRoundRect(dvdX, dvdY, DVD_W, DVD_H, 4, SSD1306_WHITE);
            display.setCursor(dvdX + 8, dvdY + 3);
            display.print(F("NEON"));
        }
        else if (mode == 2)
        {
            clearDisp();
            int cx = 64, cy = 32;
            display.drawCircle(cx, cy, 6, SSD1306_WHITE);
            display.drawCircle(cx, cy, 14, SSD1306_WHITE);
            display.drawCircle(cx, cy, 22, SSD1306_WHITE);
            display.drawCircle(cx, cy, 30, SSD1306_WHITE);
            display.drawFastHLine(0, cy, 128, SSD1306_WHITE);
            display.drawFastVLine(cx, 0, 64, SSD1306_WHITE);
            for (float r = 0; r < 30; r += 1)
            {
                int x = cx + cos(radarAngle) * r, y = cy + sin(radarAngle) * r;
                if (x >= 0 && x < 128 && y >= 0 && y < 64)
                    display.drawPixel(x, y, SSD1306_WHITE);
            }
            for (int t = 1; t <= 6; t++)
            {
                float trailAngle = radarAngle - t * 0.12f;
                for (float r = 0; r < 30 - t * 3; r += 1.5)
                {
                    int x = cx + cos(trailAngle) * r, y = cy + sin(trailAngle) * r;
                    if (x >= 0 && x < 128 && y >= 0 && y < 64)
                        display.drawPixel(x, y, SSD1306_WHITE);
                }
            }
            radarAngle += 0.12f;
            if (radarAngle > TWO_PI)
                radarAngle -= TWO_PI;
        }
        else if (mode == 3)
        {
            clearDisp();
            int cx = 64, cy = 32;
            for (int i = 0; i < 4; i++)
            {
                int phase = (ringPhase + i * 10) % 40;
                int r = 5 + phase;
                if (r < 30)
                    display.drawCircle(cx, cy, r, SSD1306_WHITE);
            }
            int pdot = 2 + ((ringPhase / 2) % 4);
            display.fillCircle(cx, cy, pdot, SSD1306_WHITE);
            ringPhase++;
            if (ringPhase > 40)
                ringPhase = 0;
        }
        display.display();
    }
}

// ============================================================
//  DINO RUNNER
// ============================================================
#define DINO_GROUND 56
#define DINO_TREX_X 12
#define DINO_JUMP_VELOCITY -10
#define DINO_GRAVITY_UP 1
#define DINO_GRAVITY_DOWN 2
#define DINO_JUMP_CUTOFF -3
#define DINO_AUTO_EXIT_MS 4000

static const uint8_t PROGMEM dinoRun1[] = {
    0x00, 0x0F, 0x80, 0x00, 0x1F, 0xC0, 0x00, 0x1D, 0xC0, 0x00, 0x1F, 0xC0,
    0x00, 0x1F, 0xC0, 0x00, 0x1F, 0x00, 0x80, 0x1F, 0x80, 0x80, 0x3F, 0xC0,
    0xC0, 0x3F, 0xC0, 0xE0, 0x7F, 0xC0, 0xF0, 0xFF, 0xC0, 0xFF, 0xFF, 0xC0,
    0x7F, 0xFF, 0xC0, 0x3F, 0xFF, 0xC0, 0x1F, 0xFF, 0x80, 0x0F, 0xFF, 0x00,
    0x07, 0xFE, 0x00, 0x07, 0x1C, 0x00, 0x06, 0x18, 0x00, 0x07, 0x00, 0x00};
static const uint8_t PROGMEM dinoRun2[] = {
    0x00, 0x0F, 0x80, 0x00, 0x1F, 0xC0, 0x00, 0x1D, 0xC0, 0x00, 0x1F, 0xC0,
    0x00, 0x1F, 0xC0, 0x00, 0x1F, 0x00, 0x80, 0x1F, 0x80, 0x80, 0x3F, 0xC0,
    0xC0, 0x3F, 0xC0, 0xE0, 0x7F, 0xC0, 0xF0, 0xFF, 0xC0, 0xFF, 0xFF, 0xC0,
    0x7F, 0xFF, 0xC0, 0x3F, 0xFF, 0xC0, 0x1F, 0xFF, 0x80, 0x0F, 0xFF, 0x00,
    0x07, 0xFE, 0x00, 0x07, 0x1C, 0x00, 0x06, 0x18, 0x00, 0x00, 0x1C, 0x00};
static const uint8_t PROGMEM dinoDuck[] = {
    0x00, 0x00, 0x70, 0x00, 0x00, 0xF0, 0x00, 0x00, 0xB0, 0x00, 0x00, 0xF0,
    0x80, 0x00, 0xF0, 0xC0, 0x01, 0xF0, 0xE2, 0xFF, 0xF0, 0xFF, 0xFF, 0xFF,
    0x7F, 0xFF, 0xFF, 0x3F, 0xFF, 0xFF, 0x0F, 0xFF, 0x00, 0x06, 0x18, 0x00};
static const uint8_t PROGMEM cactusSmall[] = {0x30, 0x30, 0x30, 0x58, 0x5A, 0x5A, 0x7A, 0x3C, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30};
static const uint8_t PROGMEM cactusBig[] = {
    0x0E, 0x00, 0x0E, 0x00, 0x0E, 0x00, 0x4E, 0x00, 0x4E, 0x40, 0x4E, 0x40,
    0x4E, 0x40, 0x4E, 0x40, 0x7C, 0x40, 0x3C, 0x40, 0x0F, 0xC0, 0x0E, 0x00,
    0x0E, 0x00, 0x0E, 0x00, 0x0E, 0x00, 0x0E, 0x00, 0x0E, 0x00, 0x0E, 0x00,
    0x0E, 0x00, 0x0E, 0x00};
static const uint8_t PROGMEM pteroUp[] = {0x00, 0x00, 0x18, 0x18, 0x3C, 0x3C, 0x7E, 0x7E, 0x3F, 0xF0, 0x1F, 0xE0, 0x0F, 0x80, 0x06, 0x00, 0x06, 0x00, 0x00, 0x00};
static const uint8_t PROGMEM pteroDn[] = {0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x0F, 0x00, 0x1F, 0x80, 0x3F, 0xF0, 0x7E, 0x7E, 0x3C, 0x3C, 0x18, 0x18, 0x00, 0x00};

uint16_t dinoHiScore = 0, dinoScore = 0;
uint8_t dinoLives = 3;
bool dHiScoreSaved = false;

struct DinoCactus
{
    bool active;
    bool big;
    int16_t x;
};
struct DinoPtero
{
    bool active;
    int16_t x, y;
    uint8_t frame, tick;
};
DinoCactus dCacti[3];
DinoPtero dPteros[2];
int16_t dTrexFeet = DINO_GROUND;
int8_t dTrexVy = 0;
bool dTrexGrounded = true, dTrexDucking = false, dTrexDead = false;
uint8_t dTrexAnimFrm = 0, dTrexAnimTck = 0, dTrexBlink = 0, dDeathTimer = 0;
uint16_t dGroundOff = 0;
int16_t dNextSpawn = 130;
uint8_t dScrollSpeed = 2;
bool dJumpPrevHeld = false;

void dinoDrawSprite(int16_t x, int16_t y, const uint8_t *data, uint8_t w, uint8_t h)
{
    int stride = (w + 7) / 8;
    for (uint8_t row = 0; row < h; row++)
        for (uint8_t b = 0; b < stride; b++)
        {
            uint8_t byte = pgm_read_byte(data + row * stride + b);
            for (uint8_t k = 0; k < 8; k++)
                if (byte & (0x80 >> k))
                {
                    int px = x + b * 8 + k, py = y + row;
                    if (px >= 0 && px < 128 && py >= 0 && py < 64)
                        display.drawPixel(px, py, SSD1306_WHITE);
                }
        }
}
void dinoSpawnCactus()
{
    for (int i = 0; i < 3; i++)
        if (!dCacti[i].active)
        {
            dCacti[i].active = true;
            dCacti[i].big = random(0, 2);
            dCacti[i].x = 132;
            return;
        }
}
void dinoSpawnPtero()
{
    for (int i = 0; i < 2; i++)
        if (!dPteros[i].active)
        {
            dPteros[i].active = true;
            dPteros[i].x = 132;
            dPteros[i].y = random(0, 2) ? 30 : 46;
            dPteros[i].frame = 0;
            dPteros[i].tick = 0;
            return;
        }
}
void dinoReset()
{
    dTrexFeet = DINO_GROUND;
    dTrexVy = 0;
    dTrexGrounded = true;
    dTrexDucking = false;
    dTrexDead = false;
    dTrexAnimFrm = 0;
    dTrexAnimTck = 0;
    dTrexBlink = 0;
    dDeathTimer = 0;
    dinoScore = 0;
    dinoLives = 3;
    dScrollSpeed = 2;
    dGroundOff = 0;
    dNextSpawn = 130;
    dHiScoreSaved = false;
    dJumpPrevHeld = false;
    for (int i = 0; i < 3; i++)
        dCacti[i].active = false;
    for (int i = 0; i < 2; i++)
        dPteros[i].active = false;
}
void dinoDrawTrex()
{
    if (dTrexBlink && (dTrexBlink & 2))
        return;
    if (dTrexDucking && dTrexGrounded)
        dinoDrawSprite(DINO_TREX_X, dTrexFeet - 12, dinoDuck, 20, 12);
    else
    {
        const uint8_t *spr = (dTrexAnimFrm & 1) ? dinoRun2 : dinoRun1;
        dinoDrawSprite(DINO_TREX_X, dTrexFeet - 20, spr, 18, 20);
    }
}
bool dinoOverlap(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh)
{
    return (ax < bx + bw) && (ax + aw > bx) && (ay < by + bh) && (ay + ah > by);
}
void dinoUpdate()
{
    dScrollSpeed = 2 + dinoScore / 300;
    if (dScrollSpeed > 6)
        dScrollSpeed = 6;
    dGroundOff = (dGroundOff + dScrollSpeed) & 127;
    if (!dTrexDead)
    {
        bool jumpHeld = (btn[BI_UP].stable == LOW);
        bool jumpPressEdge = jumpHeld && !dJumpPrevHeld;
        dJumpPrevHeld = jumpHeld;
        bool wantDuck = (btn[BI_DOWN].stable == LOW);
        if (dTrexGrounded)
        {
            if (jumpPressEdge)
            {
                dTrexVy = DINO_JUMP_VELOCITY;
                dTrexGrounded = false;
                dTrexDucking = false;
            }
            else
                dTrexDucking = wantDuck;
        }
        else if (dTrexVy < 0 && !jumpHeld && dTrexVy < DINO_JUMP_CUTOFF)
            dTrexVy = DINO_JUMP_CUTOFF;
        if (!dTrexGrounded)
        {
            dTrexFeet += dTrexVy;
            dTrexVy += (dTrexVy < 0) ? DINO_GRAVITY_UP : DINO_GRAVITY_DOWN;
            if (dTrexFeet >= DINO_GROUND)
            {
                dTrexFeet = DINO_GROUND;
                dTrexVy = 0;
                dTrexGrounded = true;
            }
        }
        if (++dTrexAnimTck >= 4)
        {
            dTrexAnimTck = 0;
            dTrexAnimFrm ^= 1;
        }
        if (dTrexBlink)
            dTrexBlink--;
    }
    else
    {
        dDeathTimer++;
        if (!dHiScoreSaved && dDeathTimer > 30)
        {
            dHiScoreSaved = true;
            if (dinoScore > dinoHiScore)
            {
                dinoHiScore = dinoScore;
                prefs.begin("trex", false);
                prefs.putUShort("hi", dinoHiScore);
                prefs.end();
            }
        }
    }
    if (!dTrexDead)
    {
        dNextSpawn -= dScrollSpeed;
        if (dNextSpawn <= 0)
        {
            if (dinoScore > 400 && random(0, 100) < 28 && !dPteros[0].active && !dPteros[1].active)
            {
                dinoSpawnPtero();
                dNextSpawn = random(150, 300);
            }
            else
            {
                dinoSpawnCactus();
                dNextSpawn = random(110, 260);
            }
        }
    }
    for (int i = 0; i < 3; i++)
    {
        if (!dCacti[i].active)
            continue;
        dCacti[i].x -= dScrollSpeed;
        if (dCacti[i].x < -14)
            dCacti[i].active = false;
    }
    for (int i = 0; i < 2; i++)
    {
        if (!dPteros[i].active)
            continue;
        dPteros[i].x -= dScrollSpeed + 2;
        if (++dPteros[i].tick >= 8)
        {
            dPteros[i].tick = 0;
            dPteros[i].frame ^= 1;
        }
        if (dPteros[i].x < -16)
            dPteros[i].active = false;
    }
    if (!dTrexDead && dTrexBlink == 0)
    {
        int hx, hy, hw, hh;
        if (dTrexDucking && dTrexGrounded)
        {
            hx = DINO_TREX_X + 2;
            hy = dTrexFeet - 8;
            hw = 16;
            hh = 8;
        }
        else
        {
            hx = DINO_TREX_X + 3;
            hy = dTrexFeet - 18;
            hw = 12;
            hh = 16;
        }
        bool hit = false;
        for (int i = 0; i < 3 && !hit; i++)
        {
            if (!dCacti[i].active)
                continue;
            int w = dCacti[i].big ? 12 : 8, h = dCacti[i].big ? 20 : 14;
            if (dinoOverlap(hx, hy, hw, hh, dCacti[i].x + 2, DINO_GROUND - h + 2, w - 4, h - 4))
                hit = true;
        }
        for (int i = 0; i < 2 && !hit; i++)
        {
            if (!dPteros[i].active)
                continue;
            if (dinoOverlap(hx, hy, hw, hh, dPteros[i].x + 2, dPteros[i].y + 2, 12, 6))
                hit = true;
        }
        if (hit)
        {
            if (dinoLives > 0)
            {
                dinoLives--;
                dTrexBlink = 90;
            }
            else
            {
                dTrexDead = true;
                dDeathTimer = 0;
            }
        }
    }
    if (!dTrexDead && dinoScore < 99999)
        dinoScore++;
}
void dinoRender(const char *overlayLine2 = nullptr)
{
    clearDisp();
    display.setCursor(0, 0);
    display.print(F("HI:"));
    display.print(dinoHiScore);
    display.print(F("  "));
    display.print(dinoScore);
    for (int i = 0; i < dinoLives; i++)
        display.fillCircle(100 + i * 8, 4, 2, SSD1306_WHITE);
    display.drawFastHLine(0, DINO_GROUND, 128, SSD1306_WHITE);
    for (int x = 0; x < 128; x += 2)
        if ((x + dGroundOff) & 2)
            display.drawPixel(x, DINO_GROUND + 1, SSD1306_WHITE);
    for (int i = 0; i < 3; i++)
    {
        if (!dCacti[i].active)
            continue;
        if (dCacti[i].big)
            dinoDrawSprite(dCacti[i].x, DINO_GROUND - 20, cactusBig, 12, 20);
        else
            dinoDrawSprite(dCacti[i].x, DINO_GROUND - 14, cactusSmall, 8, 14);
    }
    for (int i = 0; i < 2; i++)
    {
        if (!dPteros[i].active)
            continue;
        const uint8_t *spr = (dPteros[i].frame & 1) ? pteroDn : pteroUp;
        dinoDrawSprite(dPteros[i].x, dPteros[i].y, spr, 16, 10);
    }
    dinoDrawTrex();
    if (dTrexDead && dDeathTimer > 20)
    {
        display.fillRect(14, 20, 100, 24, SSD1306_BLACK);
        display.drawRect(14, 20, 100, 24, SSD1306_WHITE);
        display.setCursor(28, 24);
        display.print(F("GAME OVER"));
        if (overlayLine2)
        {
            display.setCursor(20, 34);
            display.print(overlayLine2);
        }
    }
    display.display();
}
void runDino()
{
    dinoReset();
    clearBtnFlags();
    prefs.begin("trex", true);
    dinoHiScore = prefs.getUShort("hi", 0);
    prefs.end();
    if (dinoHiScore == 0xFFFF)
        dinoHiScore = 0;
    unsigned long lastFrame = millis();
    bool deadHandled = false;
    while (true)
    {
        if (millis() - lastFrame < 33)
        {
            delay(2);
            continue;
        }
        lastFrame = millis();
        updateButtons();
        if (btn[BI_LEFT].longFired)
        {
            btn[BI_LEFT].longFired = false;
            clearBtnFlags();
            return;
        }
        if (!dTrexDead)
        {
            dinoUpdate();
            dinoRender();
            continue;
        }
        if (dDeathTimer < 60)
        {
            dinoUpdate();
            dinoRender();
            continue;
        }
        if (!deadHandled)
        {
            deadHandled = true;
            unsigned long waitStart = millis();
            bool restarted = false;
            while (true)
            {
                unsigned long elapsed = millis() - waitStart;
                if (elapsed >= DINO_AUTO_EXIT_MS)
                    break;
                updateButtons();
                if (btn[BI_LEFT].longFired)
                {
                    btn[BI_LEFT].longFired = false;
                    clearBtnFlags();
                    return;
                }
                Act a = getAction();
                if (a == A_UP || a == A_RIGHT)
                {
                    restarted = true;
                    break;
                }
                if (a == A_BACK)
                {
                    clearBtnFlags();
                    return;
                }
                char line2[24];
                int secsLeft = (DINO_AUTO_EXIT_MS - (int)elapsed) / 1000 + 1;
                snprintf(line2, sizeof(line2), "Exit in %ds..", secsLeft);
                dinoRender(line2);
                delay(30);
            }
            if (restarted)
            {
                dinoReset();
                deadHandled = false;
            }
            else
            {
                clearBtnFlags();
                return;
            }
        }
    }
}

// ============================================================
//  TETRIS
// ============================================================
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

const uint16_t TET_PIECES[7][4] = {
    {0x0F00, 0x2222, 0x00F0, 0x4444},
    {0x8E00, 0x6440, 0x0E20, 0x44C0},
    {0x2E00, 0x4460, 0x0E80, 0xC440},
    {0x6600, 0x6600, 0x6600, 0x6600},
    {0x6C00, 0x4620, 0x06C0, 0x8C40},
    {0x4E00, 0x4640, 0x0E40, 0x4C40},
    {0xC600, 0x2640, 0x0C60, 0x4C80}};
enum TetGameState : uint8_t
{
    TETST_PLAYING,
    TETST_CLEARING,
    TETST_GAMEOVER
};
uint8_t tetBoard[TET_ROWS][TET_COLS];
uint8_t tetPieceType = 0, tetPieceRot = 0, tetNextType = 0;
int8_t tetPieceX = 3, tetPieceY = 0;
uint8_t tetClearRows[4], tetClearCount = 0;
uint32_t tetClearStart = 0, tetFlashTimer = 0;
uint32_t tetScore = 0;
uint16_t tetLinesCleared = 0;
uint8_t tetLevel = 0;
uint32_t tetDropInterval = TET_BASE_DROP_MS, tetDropTimer = 0;
TetGameState tetState = TETST_PLAYING;
bool tetDirty = true;

inline bool tetCellSet(uint16_t shape, int r, int c) { return (shape >> (15 - (r * 4 + c))) & 1; }
bool tetCollides(uint8_t type, uint8_t rot, int8_t px, int8_t py)
{
    uint16_t shape = TET_PIECES[type][rot];
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
        {
            if (!tetCellSet(shape, r, c))
                continue;
            int bx = px + c, by = py + r;
            if (bx < 0 || bx >= TET_COLS)
                return true;
            if (by >= TET_ROWS)
                return true;
            if (by >= 0 && tetBoard[by][bx])
                return true;
        }
    return false;
}
void tetLockPiece();
void tetSpawnPiece()
{
    tetPieceType = tetNextType;
    tetNextType = random(0, 7);
    tetPieceRot = 0;
    tetPieceX = 3;
    uint16_t shape = TET_PIECES[tetPieceType][0];
    tetPieceY = 0;
    for (int r = 0; r < 4; r++)
    {
        bool found = false;
        for (int c = 0; c < 4; c++)
            if (tetCellSet(shape, r, c))
            {
                found = true;
                break;
            }
        if (found)
        {
            tetPieceY = -r;
            break;
        }
    }
    if (tetCollides(tetPieceType, tetPieceRot, tetPieceX, tetPieceY))
        tetState = TETST_GAMEOVER;
    else
    {
        tetState = TETST_PLAYING;
        tetDropTimer = millis();
    }
    tetDirty = true;
}
void tetRotatePiece()
{
    uint8_t nr = (tetPieceRot + 1) & 3;
    if (!tetCollides(tetPieceType, nr, tetPieceX, tetPieceY))
    {
        tetPieceRot = nr;
        return;
    }
    const int8_t kicks[4] = {-1, 1, -2, 2};
    for (int i = 0; i < 4; i++)
        if (!tetCollides(tetPieceType, nr, tetPieceX + kicks[i], tetPieceY))
        {
            tetPieceRot = nr;
            tetPieceX += kicks[i];
            return;
        }
}
void tetHardDrop()
{
    while (!tetCollides(tetPieceType, tetPieceRot, tetPieceX, tetPieceY + 1))
    {
        tetPieceY++;
        tetScore += 2;
    }
    tetLockPiece();
}
void tetLockPiece()
{
    uint16_t shape = TET_PIECES[tetPieceType][tetPieceRot];
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
        {
            if (!tetCellSet(shape, r, c))
                continue;
            int bx = tetPieceX + c, by = tetPieceY + r;
            if (by >= 0 && by < TET_ROWS && bx >= 0 && bx < TET_COLS)
                tetBoard[by][bx] = 1;
        }
    tetClearCount = 0;
    for (int r = 0; r < TET_ROWS; r++)
    {
        bool full = true;
        for (int c = 0; c < TET_COLS; c++)
            if (!tetBoard[r][c])
            {
                full = false;
                break;
            }
        if (full && tetClearCount < 4)
            tetClearRows[tetClearCount++] = r;
    }
    if (tetClearCount > 0)
    {
        tetState = TETST_CLEARING;
        tetClearStart = millis();
        tetFlashTimer = tetClearStart;
    }
    else
        tetSpawnPiece();
    tetDirty = true;
}
void tetRemoveClearedRows()
{
    for (int i = 0; i < tetClearCount - 1; i++)
        for (int j = i + 1; j < tetClearCount; j++)
            if (tetClearRows[j] < tetClearRows[i])
            {
                uint8_t t = tetClearRows[i];
                tetClearRows[i] = tetClearRows[j];
                tetClearRows[j] = t;
            }
    for (int i = tetClearCount - 1; i >= 0; i--)
    {
        int row = tetClearRows[i];
        for (int r = row; r > 0; r--)
            for (int c = 0; c < TET_COLS; c++)
                tetBoard[r][c] = tetBoard[r - 1][c];
        for (int c = 0; c < TET_COLS; c++)
            tetBoard[0][c] = 0;
    }
    tetClearCount = 0;
}
void tetApplyScore(uint8_t n)
{
    static const uint16_t pts[5] = {0, 100, 300, 500, 800};
    if (n > 4)
        n = 4;
    tetScore += (uint32_t)pts[n] * (uint32_t)(tetLevel + 1);
    tetLinesCleared += n;
    uint8_t newLevel = tetLinesCleared / TET_LINES_PER_LEVEL;
    if (newLevel != tetLevel)
    {
        tetLevel = newLevel;
        tetDropInterval = TET_BASE_DROP_MS - (uint32_t)tetLevel * TET_DROP_STEP_MS;
        if (tetDropInterval < TET_MIN_DROP_MS)
            tetDropInterval = TET_MIN_DROP_MS;
    }
}
void tetResetGame()
{
    memset(tetBoard, 0, sizeof(tetBoard));
    tetScore = 0;
    tetLinesCleared = 0;
    tetLevel = 0;
    tetDropInterval = TET_BASE_DROP_MS;
    tetClearCount = 0;
    tetNextType = random(0, 7);
    tetSpawnPiece();
    tetDropTimer = millis();
    tetDirty = true;
}
void tetDrawPreview(uint8_t type)
{
    uint16_t shape = TET_PIECES[type][0];
    int minR = 4, maxR = -1, minC = 4, maxC = -1;
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            if (tetCellSet(shape, r, c))
            {
                if (r < minR)
                    minR = r;
                if (r > maxR)
                    maxR = r;
                if (c < minC)
                    minC = c;
                if (c > maxC)
                    maxC = c;
            }
    if (maxR < 0)
        return;
    const int BS = 4;
    int w = (maxC - minC + 1) * BS, h = (maxR - minR + 1) * BS;
    int ox = 35 + (20 - w) / 2, oy = 9 + (20 - h) / 2;
    for (int r = minR; r <= maxR; r++)
        for (int c = minC; c <= maxC; c++)
            if (tetCellSet(shape, r, c))
                display.fillRect(ox + (c - minC) * BS, oy + (r - minR) * BS, BS, BS, SSD1306_WHITE);
}
void tetDrawField()
{
    display.drawRect(TET_FIELD_X - 1, TET_FIELD_Y - 1, TET_COLS * TET_BLOCK + 2, TET_ROWS * TET_BLOCK + 2, SSD1306_WHITE);
    bool flashPhase = false;
    if (tetState == TETST_CLEARING)
        flashPhase = ((millis() - tetClearStart) / TET_FLASH_MS) & 1;
    for (int r = 0; r < TET_ROWS; r++)
    {
        bool rowClearing = false;
        if (tetState == TETST_CLEARING)
            for (int i = 0; i < tetClearCount; i++)
                if (tetClearRows[i] == r)
                {
                    rowClearing = true;
                    break;
                }
        if (rowClearing && flashPhase)
            continue;
        for (int c = 0; c < TET_COLS; c++)
        {
            if (!tetBoard[r][c])
                continue;
            display.fillRect(TET_FIELD_X + c * TET_BLOCK, TET_FIELD_Y + r * TET_BLOCK, TET_BLOCK, TET_BLOCK, SSD1306_WHITE);
        }
    }
    if (tetState == TETST_PLAYING)
    {
        uint16_t shape = TET_PIECES[tetPieceType][tetPieceRot];
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 4; c++)
            {
                if (!tetCellSet(shape, r, c))
                    continue;
                int bx = tetPieceX + c, by = tetPieceY + r;
                if (by < 0 || by >= TET_ROWS || bx < 0 || bx >= TET_COLS)
                    continue;
                display.fillRect(TET_FIELD_X + bx * TET_BLOCK, TET_FIELD_Y + by * TET_BLOCK, TET_BLOCK, TET_BLOCK, SSD1306_WHITE);
            }
    }
}
void tetDrawPanel()
{
    char buf[16];
    display.setTextSize(1);
    display.setCursor(36, 0);
    display.print(F("NEXT"));
    display.drawRect(34, 8, 22, 22, SSD1306_WHITE);
    tetDrawPreview(tetNextType);
    display.setCursor(60, 8);
    display.print(F("SCORE"));
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)tetScore);
    int w = (int)strlen(buf) * 12;
    int sx = SCREEN_W - w;
    if (sx < 58)
        sx = 58;
    display.setTextSize(2);
    display.setCursor(sx, 18);
    display.print(buf);
    display.setTextSize(1);
    display.setCursor(60, 36);
    display.print(F("LEVEL"));
    snprintf(buf, sizeof(buf), "%u", (unsigned)tetLevel);
    display.setTextSize(2);
    display.setCursor(60, 46);
    display.print(buf);
}
void tetDrawGameOver()
{
    char buf[24];
    display.setTextSize(2);
    display.setCursor(10, 8);
    display.print(F("GAME OVER"));
    display.setTextSize(1);
    snprintf(buf, sizeof(buf), "SCORE %lu", (unsigned long)tetScore);
    int x = (SCREEN_W - (int)strlen(buf) * 6) / 2;
    if (x < 0)
        x = 0;
    display.setCursor(x, 34);
    display.print(buf);
    display.setCursor(10, 46);
    display.print(F("UP/RIGHT:Restart"));
    display.setCursor(10, 56);
    display.print(F("Long LEFT:Exit"));
}
void tetRender()
{
    clearDisp();
    if (tetState == TETST_GAMEOVER)
        tetDrawGameOver();
    else
    {
        tetDrawField();
        tetDrawPanel();
    }
    display.display();
    tetDirty = false;
}
void runTetris()
{
    tetResetGame();
    clearBtnFlags();
    unsigned long lastLeftRepeat = 0, lastRightRepeat = 0, lastDownRepeat = 0;
    bool leftHeldPrev = false, rightHeldPrev = false, downHeldPrev = false, upHeldPrev = false;
    while (true)
    {
        uint32_t now = millis();
        updateButtons();
        if (btn[BI_LEFT].longFired)
        {
            btn[BI_LEFT].longFired = false;
            clearBtnFlags();
            return;
        }
        bool leftHeld = (btn[BI_LEFT].stable == LOW);
        bool rightHeld = (btn[BI_RIGHT].stable == LOW);
        bool downHeld = (btn[BI_DOWN].stable == LOW);
        bool upHeld = (btn[BI_UP].stable == LOW);
        bool upEdge = upHeld && !upHeldPrev;
        upHeldPrev = upHeld;
        if (btn[BI_RIGHT].longFired)
        {
            btn[BI_RIGHT].longFired = false;
            if (tetState == TETST_PLAYING)
            {
                tetHardDrop();
                tetDirty = true;
            }
        }
        if (tetState == TETST_GAMEOVER)
        {
            bool rightEdge = rightHeld && !rightHeldPrev;
            rightHeldPrev = rightHeld;
            if (upEdge || rightEdge)
            {
                tetResetGame();
            }
            if (tetDirty)
                tetRender();
            delay(20);
            continue;
        }
        if (tetState == TETST_PLAYING)
        {
            if (upEdge)
            {
                tetRotatePiece();
                tetDirty = true;
            }
            if (leftHeld)
            {
                bool fire = false;
                if (!leftHeldPrev)
                {
                    fire = true;
                    lastLeftRepeat = now + TET_REPEAT_DELAY_MS;
                }
                else if ((int32_t)(now - lastLeftRepeat) >= 0)
                {
                    fire = true;
                    lastLeftRepeat = now + TET_REPEAT_RATE_MS;
                }
                if (fire && !tetCollides(tetPieceType, tetPieceRot, tetPieceX - 1, tetPieceY))
                {
                    tetPieceX--;
                    tetDirty = true;
                }
            }
            leftHeldPrev = leftHeld;
            if (rightHeld && !btn[BI_RIGHT].longFired)
            {
                bool fire = false;
                if (!rightHeldPrev)
                {
                    fire = true;
                    lastRightRepeat = now + TET_REPEAT_DELAY_MS;
                }
                else if ((int32_t)(now - lastRightRepeat) >= 0)
                {
                    fire = true;
                    lastRightRepeat = now + TET_REPEAT_RATE_MS;
                }
                if (fire && !tetCollides(tetPieceType, tetPieceRot, tetPieceX + 1, tetPieceY))
                {
                    tetPieceX++;
                    tetDirty = true;
                }
            }
            rightHeldPrev = rightHeld;
            if (downHeld)
            {
                bool fire = false;
                if (!downHeldPrev)
                {
                    fire = true;
                    lastDownRepeat = now + TET_REPEAT_DELAY_MS;
                }
                else if ((int32_t)(now - lastDownRepeat) >= 0)
                {
                    fire = true;
                    lastDownRepeat = now + TET_REPEAT_RATE_MS;
                }
                if (fire)
                {
                    if (!tetCollides(tetPieceType, tetPieceRot, tetPieceX, tetPieceY + 1))
                    {
                        tetPieceY++;
                        tetScore += 1;
                    }
                    else
                        tetLockPiece();
                    tetDirty = true;
                }
            }
            downHeldPrev = downHeld;
            if (tetState == TETST_PLAYING && (now - tetDropTimer >= tetDropInterval))
            {
                tetDropTimer = now;
                if (!tetCollides(tetPieceType, tetPieceRot, tetPieceX, tetPieceY + 1))
                    tetPieceY++;
                else
                    tetLockPiece();
                tetDirty = true;
            }
        }
        else if (tetState == TETST_CLEARING)
        {
            if (now - tetClearStart >= TET_CLEAR_MS)
            {
                uint8_t n = tetClearCount;
                tetRemoveClearedRows();
                tetApplyScore(n);
                tetSpawnPiece();
                tetDirty = true;
            }
            else if (now - tetFlashTimer >= TET_FLASH_MS)
            {
                tetFlashTimer = now;
                tetDirty = true;
            }
        }
        if (tetDirty)
            tetRender();
        delay(8);
    }
}

// ============================================================
//  SNAKE
// ============================================================
#define SNK_COLS 24
#define SNK_ROWS 8
#define SNK_CELL 5
#define SNK_OFF_X 4
#define SNK_OFF_Y 13
#define SNK_MAX_LEN 240
#define SNK_MAX_PARTICLES 24
#define SNK_START_STEP_MS 220
#define SNK_MIN_STEP_MS 70
#define SNK_STEP_DECREMENT 6
#define SNK_PAUSE_QUIT_MS 6000
#define SNK_AUTO_QUIT_MS 5000

enum SnkState : uint8_t
{
    SNK_ST_MENU,
    SNK_ST_PLAY,
    SNK_ST_PAUSE,
    SNK_ST_OVER
};
struct SnkSeg
{
    int8_t x, y;
};
struct SnkParticle
{
    float x, y, vx, vy;
    uint8_t life;
};

SnkSeg snkBody[SNK_MAX_LEN];
SnkSeg snkBodyPrev[SNK_MAX_LEN];
uint16_t snkLen = 0, snkLenPrev = 0;
int8_t snkDirX = 1, snkDirY = 0;
int8_t snkPendingDirX = 1, snkPendingDirY = 0;
int8_t snkFoodX = 0, snkFoodY = 0;
uint16_t snkScore = 0, snkHiScore = 0;
uint32_t snkStepMs = SNK_START_STEP_MS, snkLastStep = 0;
SnkState snkState = SNK_ST_MENU;
uint32_t snkStateTimer = 0;
uint8_t snkShakeMag = 0;
SnkParticle snkParticles[SNK_MAX_PARTICLES];

void snkSpawnParticles(int8_t gx, int8_t gy, uint8_t count)
{
    float px = SNK_OFF_X + gx * SNK_CELL + SNK_CELL / 2.0f;
    float py = SNK_OFF_Y + gy * SNK_CELL + SNK_CELL / 2.0f;
    for (int i = 0; i < SNK_MAX_PARTICLES && count > 0; i++)
    {
        if (snkParticles[i].life > 0)
            continue;
        float ang = random(0, 628) / 100.0f;
        float spd = 0.5f + random(0, 150) / 100.0f;
        snkParticles[i].x = px;
        snkParticles[i].y = py;
        snkParticles[i].vx = cos(ang) * spd;
        snkParticles[i].vy = sin(ang) * spd;
        snkParticles[i].life = 12 + random(0, 10);
        count--;
    }
}
void snkUpdateParticles()
{
    for (int i = 0; i < SNK_MAX_PARTICLES; i++)
    {
        if (snkParticles[i].life == 0)
            continue;
        snkParticles[i].x += snkParticles[i].vx;
        snkParticles[i].y += snkParticles[i].vy;
        snkParticles[i].vy += 0.06f;
        snkParticles[i].life--;
    }
}
void snkDrawParticles(int ox, int oy)
{
    for (int i = 0; i < SNK_MAX_PARTICLES; i++)
    {
        if (snkParticles[i].life == 0)
            continue;
        int px = (int)snkParticles[i].x + ox, py = (int)snkParticles[i].y + oy;
        if (px >= 0 && px < 128 && py >= 0 && py < 64)
            display.drawPixel(px, py, SSD1306_WHITE);
    }
}
bool snkCollidesWithSelf(int8_t x, int8_t y)
{
    for (int i = 0; i < snkLen; i++)
        if (snkBody[i].x == x && snkBody[i].y == y)
            return true;
    return false;
}
void snkReset()
{
    snkLen = 4;
    for (int i = 0; i < snkLen; i++)
    {
        snkBody[i].x = 6 - i;
        snkBody[i].y = SNK_ROWS / 2;
    }
    memcpy(snkBodyPrev, snkBody, sizeof(SnkSeg) * snkLen);
    snkLenPrev = snkLen;
    snkDirX = 1;
    snkDirY = 0;
    snkPendingDirX = 1;
    snkPendingDirY = 0;
    snkScore = 0;
    snkStepMs = SNK_START_STEP_MS;
    snkLastStep = millis();
    snkShakeMag = 0;
    for (int i = 0; i < SNK_MAX_PARTICLES; i++)
        snkParticles[i].life = 0;
    int tries = 0;
    do
    {
        snkFoodX = random(0, SNK_COLS);
        snkFoodY = random(0, SNK_ROWS);
        tries++;
    } while (snkCollidesWithSelf(snkFoodX, snkFoodY) && tries < 100);
}
void snkDeath()
{
    snkState = SNK_ST_OVER;
    snkStateTimer = millis();
    snkShakeMag = 6;
    snkSpawnParticles(snkBody[0].x, snkBody[0].y, 16);
    if (snkScore > snkHiScore)
    {
        snkHiScore = snkScore;
        prefs.begin("snake", false);
        prefs.putUShort("hi", snkHiScore);
        prefs.end();
    }
}
void snkStep()
{
    memcpy(snkBodyPrev, snkBody, sizeof(SnkSeg) * snkLen);
    snkLenPrev = snkLen;
    snkDirX = snkPendingDirX;
    snkDirY = snkPendingDirY;
    int8_t nx = snkBody[0].x + snkDirX, ny = snkBody[0].y + snkDirY;
    if (nx < 0 || nx >= SNK_COLS || ny < 0 || ny >= SNK_ROWS)
    {
        snkDeath();
        return;
    }
    for (int i = 0; i < snkLen - 1; i++)
        if (snkBody[i].x == nx && snkBody[i].y == ny)
        {
            snkDeath();
            return;
        }
    bool ate = (nx == snkFoodX && ny == snkFoodY);
    if (!ate)
    {
        for (int i = snkLen - 1; i > 0; i--)
            snkBody[i] = snkBody[i - 1];
        snkBody[0].x = nx;
        snkBody[0].y = ny;
    }
    else
    {
        if (snkLen < SNK_MAX_LEN)
        {
            for (int i = snkLen; i > 0; i--)
                snkBody[i] = snkBody[i - 1];
            snkLen++;
            snkBody[0].x = nx;
            snkBody[0].y = ny;
        }
        snkScore++;
        snkSpawnParticles(snkFoodX, snkFoodY, 8);
        snkShakeMag = 3;
        if (snkStepMs > SNK_MIN_STEP_MS + SNK_STEP_DECREMENT)
            snkStepMs -= SNK_STEP_DECREMENT;
        else
            snkStepMs = SNK_MIN_STEP_MS;
        int tries = 0;
        do
        {
            snkFoodX = random(0, SNK_COLS);
            snkFoodY = random(0, SNK_ROWS);
            tries++;
        } while (snkCollidesWithSelf(snkFoodX, snkFoodY) && tries < 200);
    }
}
void snkRender(float stepT)
{
    clearDisp();
    int shakeX = 0, shakeY = 0;
    if (snkShakeMag > 0)
    {
        shakeX = random(-(int)snkShakeMag, (int)snkShakeMag + 1);
        shakeY = random(-(int)snkShakeMag, (int)snkShakeMag + 1);
    }
    display.setCursor(2, 1);
    display.print(F("SNK  HI:"));
    display.print(snkHiScore);
    display.print(F("  "));
    display.print(snkScore);
    display.drawFastHLine(0, 10, 128, SSD1306_WHITE);
    display.drawRect(SNK_OFF_X - 1 + shakeX, SNK_OFF_Y - 1 + shakeY, SNK_COLS * SNK_CELL + 2, SNK_ROWS * SNK_CELL + 2, SSD1306_WHITE);
    float pulse = 0.5f + 0.5f * sinf(millis() / 200.0f);
    int foodSize = 1 + (int)(pulse * 2.0f);
    int fx = SNK_OFF_X + snkFoodX * SNK_CELL + SNK_CELL / 2 + shakeX;
    int fy = SNK_OFF_Y + snkFoodY * SNK_CELL + SNK_CELL / 2 + shakeY;
    display.fillCircle(fx, fy, foodSize, SSD1306_WHITE);
    for (int i = (int)snkLen - 1; i >= 0; i--)
    {
        float x, y;
        if (i < (int)snkLenPrev && i < (int)snkLen)
        {
            x = snkBodyPrev[i].x + (snkBody[i].x - snkBodyPrev[i].x) * stepT;
            y = snkBodyPrev[i].y + (snkBody[i].y - snkBodyPrev[i].y) * stepT;
        }
        else
        {
            x = snkBody[i].x;
            y = snkBody[i].y;
        }
        int px = SNK_OFF_X + (int)(x * SNK_CELL) + shakeX;
        int py = SNK_OFF_Y + (int)(y * SNK_CELL) + shakeY;
        display.fillRect(px, py, SNK_CELL - 1, SNK_CELL - 1, SSD1306_WHITE);
    }
    snkDrawParticles(shakeX, shakeY);
    if (snkState == SNK_ST_MENU)
    {
        display.fillRect(20, 22, 88, 24, SSD1306_BLACK);
        display.drawRect(20, 22, 88, 24, SSD1306_WHITE);
        display.setCursor(52, 26);
        display.print(F("SNAKE"));
        display.setCursor(28, 36);
        display.print(F("RIGHT: Start"));
        drawHint("L-LEFT:Exit");
    }
    else if (snkState == SNK_ST_PAUSE)
    {
        display.fillRect(20, 22, 88, 24, SSD1306_BLACK);
        display.drawRect(20, 22, 88, 24, SSD1306_WHITE);
        display.setCursor(46, 26);
        display.print(F("PAUSED"));
        display.setCursor(26, 36);
        display.print(F("RIGHT: Resume"));
        drawHint("L-RIGHT:Pause L-LEFT:Exit");
    }
    else if (snkState == SNK_ST_OVER)
    {
        display.fillRect(14, 18, 100, 32, SSD1306_BLACK);
        display.drawRect(14, 18, 100, 32, SSD1306_WHITE);
        display.setCursor(28, 22);
        display.print(F("GAME OVER"));
        display.setCursor(24, 32);
        display.print(F("RIGHT: Restart"));
        drawHint("L-LEFT:Exit");
    }
    display.display();
    if (snkShakeMag > 0)
        snkShakeMag--;
}
void runSnake()
{
    prefs.begin("snake", true);
    snkHiScore = prefs.getUShort("hi", 0);
    prefs.end();
    if (snkHiScore == 0xFFFF)
        snkHiScore = 0;
    snkState = SNK_ST_MENU;
    snkReset();
    clearBtnFlags();
    unsigned long lastFrame = millis();
    while (true)
    {
        Act a = getAction();
        if (a == A_BACK)
        {
            clearBtnFlags();
            return;
        }
        if (snkState == SNK_ST_MENU)
        {
            if (a == A_RIGHT)
            {
                snkReset();
                snkState = SNK_ST_PLAY;
                snkLastStep = millis();
                clearBtnFlags();
            }
        }
        else if (snkState == SNK_ST_PLAY)
        {
            if (a == A_UP && snkDirY == 0)
            {
                snkPendingDirX = 0;
                snkPendingDirY = -1;
            }
            else if (a == A_DOWN && snkDirY == 0)
            {
                snkPendingDirX = 0;
                snkPendingDirY = 1;
            }
            else if (a == A_LEFT && snkDirX == 0)
            {
                snkPendingDirX = -1;
                snkPendingDirY = 0;
            }
            else if (a == A_RIGHT && snkDirX == 0)
            {
                snkPendingDirX = 1;
                snkPendingDirY = 0;
            }
            else if (a == A_ALT_RIGHT)
            {
                snkState = SNK_ST_PAUSE;
                snkStateTimer = millis();
            }
            uint32_t now = millis();
            if (now - snkLastStep >= snkStepMs)
            {
                snkLastStep = now;
                snkStep();
            }
        }
        else if (snkState == SNK_ST_PAUSE)
        {
            if (a == A_ALT_RIGHT || a == A_RIGHT)
            {
                snkState = SNK_ST_PLAY;
                snkLastStep = millis();
            }
            if (millis() - snkStateTimer > SNK_PAUSE_QUIT_MS)
            {
                clearBtnFlags();
                return;
            }
        }
        else if (snkState == SNK_ST_OVER)
        {
            if (a == A_RIGHT)
            {
                snkReset();
                snkState = SNK_ST_PLAY;
                snkLastStep = millis();
                clearBtnFlags();
            }
            if (millis() - snkStateTimer > SNK_AUTO_QUIT_MS)
            {
                clearBtnFlags();
                return;
            }
        }
        if (millis() - lastFrame >= 16)
        {
            lastFrame = millis();
            float stepT = 0.0f;
            if (snkState == SNK_ST_PLAY)
            {
                stepT = (float)(millis() - snkLastStep) / (float)snkStepMs;
                if (stepT > 1.0f)
                    stepT = 1.0f;
            }
            snkUpdateParticles();
            snkRender(stepT);
        }
    }
}

// ============================================================
//  CAT PET APP
// ============================================================
enum CatScene
{
    CAT_HOME,
    CAT_MENU,
    CAT_STATS,
    CAT_STORE,
    CAT_GARDEN,
    CAT_MINIGAMES
};

struct CatSaveBlob
{
    uint8_t stats[18];
    uint8_t coins;
    uint8_t food;
    uint8_t seeds;
    uint8_t water;
    uint8_t weather;
    uint8_t selectedSeed;
    uint8_t selectedPot;
    uint8_t _pad[2];
    Plant garden[3];
};

CatPetData catPet;
bool catPetLoaded = false;
CatScene catScene = CAT_HOME;
uint8_t catMenuSelection = 0;
uint8_t catStoreSelection = 0;
uint8_t catMinigameSelection = 0;
uint8_t catStatsPage = 0;
uint8_t catGardenSelection = 0;
int catWorldX = 64;
uint32_t catLastRapidMs = 0;
uint32_t catLastMediumMs = 0;
uint32_t catLastSlowMs = 0;
uint32_t catLastVerySlowMs = 0;
unsigned long catLastSaveMs = 0;
unsigned long catLastWeatherMs = 0;
CatBehavior currentBehavior = B_IDLE;
uint32_t catBehaviorStartedMs = 0;
uint32_t catBehaviorDurationMs = 5000;
uint32_t catLastFrameUpdate = 0;
uint8_t catCurrentFrame = 0;

const char *CAT_MENU_ITEMS[] = {"Home", "Stats", "Store", "Garden", "Minigames"};
const char *CAT_STORE_ITEMS[] = {"Food  - 5 coins", "Seed  - 8 coins", "Water - 3 coins"};
const char *CAT_MINIGAME_ITEMS[] = {"Dino Runner", "Tetris", "Snake"};
const char *CAT_BEHAVIOR_NAMES[] = {"IDLE", "SLEEP", "NAP", "STRETCH", "KNEAD", "LOUNGE", "INVEST", "OBSERVE", "CHAT", "ZOOM", "VOCAL", "GROOM", "GROOMED", "HUNT", "GIFT", "PACE", "SULK", "MISCHIEF", "HIDE", "TRAIN", "PLAY", "LOVE", "ATTN", "EAT", "START", "WANDER"};
const uint8_t CAT_STAT_COUNT = 18;
const uint8_t CAT_WEATHER_COUNT = 4;
static_assert(sizeof(CAT_BEHAVIOR_NAMES) / sizeof(CAT_BEHAVIOR_NAMES[0]) == 26,
              "CAT_BEHAVIOR_NAMES length must match CatBehavior enum count");
static_assert(B_MEANDERING + 1 == 26, "CatBehavior enum must contain 26 values");

const char *behaviorName(CatBehavior behavior)
{
    switch (behavior)
    {
    case B_IDLE:
        return "IDLE";
    case B_SLEEPING:
        return "SLEEP";
    case B_NAPPING:
        return "NAP";
    case B_STRETCHING:
        return "STRETCH";
    case B_KNEADING:
        return "KNEAD";
    case B_LOUNGING:
        return "LOUNGE";
    case B_INVESTIGATING:
        return "INVEST";
    case B_OBSERVING:
        return "OBSERVE";
    case B_CHATTERING:
        return "CHAT";
    case B_ZOOMIES:
        return "ZOOM";
    case B_VOCALIZING:
        return "VOCAL";
    case B_SELF_GROOMING:
        return "GROOM";
    case B_BEING_GROOMED:
        return "GROOMED";
    case B_HUNTING:
        return "HUNT";
    case B_GIFT_BRINGING:
        return "GIFT";
    case B_PACING:
        return "PACE";
    case B_SULKING:
        return "SULK";
    case B_MISCHIEF:
        return "MISCHIEF";
    case B_HIDING:
        return "HIDE";
    case B_TRAINING:
        return "TRAIN";
    case B_PLAYING:
        return "PLAY";
    case B_AFFECTION:
        return "LOVE";
    case B_ATTENTION:
        return "ATTN";
    case B_EATING:
        return "EAT";
    case B_STARTLED:
        return "START";
    case B_MEANDERING:
        return "WANDER";
    default:
        return "???";
    }
}

const char *catStatKey(uint8_t index)
{
    switch (index)
    {
    case 0:
        return "hunger";
    case 1:
        return "energy";
    case 2:
        return "health";
    case 3:
        return "mood";
    case 4:
        return "clean";
    case 5:
        return "affect";
    case 6:
        return "fitness";
    case 7:
        return "serenity";
    case 8:
        return "courage";
    case 9:
        return "loyalty";
    case 10:
        return "mischief";
    case 11:
        return "curiosity";
    case 12:
        return "social";
    case 13:
        return "intellect";
    case 14:
        return "maturity";
    case 15:
        return "fulfill";
    case 16:
        return "play";
    default:
        return "focus";
    }
}

const char *catSeedName(SeedKind seed)
{
    switch (seed)
    {
    case SEED_CAT_GRASS:
        return "Grass";
    case SEED_FREESIA:
        return "Freesia";
    case SEED_SUNFLOWER:
        return "Sunflower";
    case SEED_ROSE:
        return "Rose";
    default:
        return "?";
    }
}

const char *catStageName(PlantStage stage)
{
    switch (stage)
    {
    case STAGE_EMPTY:
        return "Empty";
    case STAGE_SEEDLING:
        return "Seedling";
    case STAGE_YOUNG:
        return "Young";
    case STAGE_GROWING:
        return "Growing";
    case STAGE_MATURE:
        return "Mature";
    case STAGE_THRIVING:
        return "Thriving";
    case STAGE_WILTING:
        return "Wilting";
    case STAGE_DEAD:
        return "Dead";
    default:
        return "?";
    }
}

const char *catPotName(PotKind pot)
{
    switch (pot)
    {
    case POT_SMALL:
        return "Small";
    case POT_MEDIUM:
        return "Medium";
    case POT_LARGE:
        return "Large";
    case POT_PLANTER:
        return "Planter";
    default:
        return "?";
    }
}

const char *catWeatherName(uint8_t weather)
{
    switch (weather)
    {
    case 0:
        return "Clear";
    case 1:
        return "Cloudy";
    case 2:
        return "Rain";
    case 3:
        return "Snow";
    default:
        return "?";
    }
}

const char *catWeatherShortName(uint8_t weather)
{
    switch (weather)
    {
    case 0:
        return "CLR";
    case 1:
        return "CLD";
    case 2:
        return "RAIN";
    case 3:
        return "SNOW";
    default:
        return "?";
    }
}

uint8_t catClamp(int value)
{
    return (uint8_t)constrain(value, 0, 100);
}

uint8_t catGetStat(uint8_t index)
{
    switch (index)
    {
    case 0:
        return catPet.hunger;
    case 1:
        return catPet.energy;
    case 2:
        return catPet.health;
    case 3:
        return catPet.mood;
    case 4:
        return catPet.cleanliness;
    case 5:
        return catPet.affection;
    case 6:
        return catPet.fitness;
    case 7:
        return catPet.serenity;
    case 8:
        return catPet.courage;
    case 9:
        return catPet.loyalty;
    case 10:
        return catPet.mischievousness;
    case 11:
        return catPet.curiosity;
    case 12:
        return catPet.sociability;
    case 13:
        return catPet.intelligence;
    case 14:
        return catPet.maturity;
    case 15:
        return catPet.fulfillment;
    case 16:
        return catPet.playfulness;
    default:
        return catPet.focus;
    }
}

void catSetStat(uint8_t index, uint8_t value)
{
    switch (index)
    {
    case 0:
        catPet.hunger = value;
        break;
    case 1:
        catPet.energy = value;
        break;
    case 2:
        catPet.health = value;
        break;
    case 3:
        catPet.mood = value;
        break;
    case 4:
        catPet.cleanliness = value;
        break;
    case 5:
        catPet.affection = value;
        break;
    case 6:
        catPet.fitness = value;
        break;
    case 7:
        catPet.serenity = value;
        break;
    case 8:
        catPet.courage = value;
        break;
    case 9:
        catPet.loyalty = value;
        break;
    case 10:
        catPet.mischievousness = value;
        break;
    case 11:
        catPet.curiosity = value;
        break;
    case 12:
        catPet.sociability = value;
        break;
    case 13:
        catPet.intelligence = value;
        break;
    case 14:
        catPet.maturity = value;
        break;
    case 15:
        catPet.fulfillment = value;
        break;
    case 16:
        catPet.playfulness = value;
        break;
    default:
        catPet.focus = value;
        break;
    }
}

uint8_t calculateCatHealth()
{
    uint16_t weighted = (uint16_t)catPet.hunger * 2 + catPet.energy +
                        (uint16_t)catPet.mood * 2 + catPet.cleanliness +
                        catPet.fitness + catPet.serenity;
    return (uint8_t)(weighted / 8);
}

void saveCatPet()
{
    CatSaveBlob blob = {};
    for (uint8_t i = 0; i < CAT_STAT_COUNT; i++)
        blob.stats[i] = catGetStat(i);
    blob.coins = catPet.coins;
    blob.food = catPet.food;
    blob.seeds = catPet.seeds;
    blob.water = catPet.water;
    blob.weather = catPet.weather;
    blob.selectedSeed = (uint8_t)catPet.selectedSeed;
    blob.selectedPot = (uint8_t)catPet.selectedPot;
    for (uint8_t i = 0; i < 3; i++)
    {
        blob.garden[i] = catPet.garden[i];
        blob.garden[i].lastUpdate = 0;
    }

    // Plant timers are zeroed on save and reset to now on load. Plants pause
    // while powered off, avoiding the frustration of returning to dead plants.
    prefs.begin("catpet", false);
    prefs.putBytes("blob", &blob, sizeof(blob));
    prefs.end();
    catLastSaveMs = millis();
}

void loadCatPet()
{
    CatSaveBlob blob = {};
    prefs.begin("catpet", true);
    size_t bytesRead = prefs.getBytes("blob", &blob, sizeof(blob));
    prefs.end();

    uint32_t now = millis();
    if (bytesRead == sizeof(blob))
    {
        for (uint8_t i = 0; i < CAT_STAT_COUNT; i++)
            catSetStat(i, blob.stats[i]);
        catPet.coins = blob.coins;
        catPet.food = blob.food;
        catPet.seeds = blob.seeds;
        catPet.water = blob.water;
        catPet.weather = blob.weather % CAT_WEATHER_COUNT;
        catPet.selectedSeed = (SeedKind)(blob.selectedSeed % 4);
        catPet.selectedPot = (PotKind)(blob.selectedPot % 4);
        for (uint8_t i = 0; i < 3; i++)
        {
            catPet.garden[i] = blob.garden[i];
            if ((uint8_t)catPet.garden[i].pot >= 4)
                catPet.garden[i].pot = POT_MEDIUM;
            if ((uint8_t)catPet.garden[i].seed >= 4)
                catPet.garden[i].seed = SEED_SUNFLOWER;
            if ((uint8_t)catPet.garden[i].stage >= 8)
                catPet.garden[i].stage = STAGE_EMPTY;
            catPet.garden[i].water = catClamp(catPet.garden[i].water);
            catPet.garden[i].growth = catClamp(catPet.garden[i].growth);
        }
    }
    for (uint8_t i = 0; i < 3; i++)
        catPet.garden[i].lastUpdate = now;

    catLastRapidMs = now;
    catLastMediumMs = now;
    catLastSlowMs = now;
    catLastVerySlowMs = now;
    catLastWeatherMs = now;
    catLastSaveMs = now;
    catBehaviorStartedMs = now;
    catBehaviorDurationMs = random(3000, 10000);
}

CatBehavior selectNextBehavior()
{
    uint8_t roll = random(100);
    if (catPet.energy < 18)
        return roll < 65 ? B_SLEEPING : B_NAPPING;
    if (catPet.hunger < 18 && catPet.food == 0)
        return roll < 60 ? B_SULKING : B_ATTENTION;
    if (catPet.cleanliness < 18)
        return B_SELF_GROOMING;
    if (catPet.mood < 25)
        return roll < 45 ? B_SULKING : (roll < 75 ? B_HIDING : B_MEANDERING);
    if (catPet.mood > 80)
    {
        if (roll < 12)
            return B_ZOOMIES;
        if (roll < 22)
            return B_KNEADING;
        if (roll < 35)
            return B_PLAYING;
    }
    if (roll < 5)
        return B_STRETCHING;
    if (roll < 10)
        return B_LOUNGING;
    if (roll < 15)
        return B_INVESTIGATING;
    if (roll < 20)
        return B_OBSERVING;
    if (roll < 25)
        return B_CHATTERING;
    if (roll < 30)
        return B_VOCALIZING;
    if (roll < 35)
        return B_HUNTING;
    if (roll < 39)
        return B_GIFT_BRINGING;
    if (roll < 44)
        return B_PACING;
    if (roll < 49)
        return B_MISCHIEF;
    if (roll < 54)
        return B_TRAINING;
    if (roll < 59)
        return B_AFFECTION;
    if (roll < 64)
        return B_ATTENTION;
    if (roll < 69)
        return B_STARTLED;
    if (roll < 74)
        return B_MEANDERING;
    if (roll < 80)
        return B_EATING;
    if (roll < 84)
        return B_BEING_GROOMED;
    if (roll < 88)
        return B_NAPPING;
    return B_IDLE;
}

void applyBehaviorChange(CatBehavior behavior)
{
    switch (behavior)
    {
    case B_ZOOMIES:
        catPet.energy = catClamp(catPet.energy - 2);
        catPet.playfulness = catClamp(catPet.playfulness + 2);
        break;
    case B_HUNTING:
        catPet.coins = catClamp(catPet.coins + 1);
        catPet.fitness = catClamp(catPet.fitness + 1);
        break;
    case B_GIFT_BRINGING:
        catPet.mood = catClamp(catPet.mood + 3);
        catPet.fulfillment = catClamp(catPet.fulfillment + 2);
        break;
    case B_KNEADING:
        catPet.affection = catClamp(catPet.affection + 2);
        catPet.serenity = catClamp(catPet.serenity + 1);
        break;
    case B_SULKING:
        catPet.mood = catClamp(catPet.mood - 1);
        break;
    case B_INVESTIGATING:
        catPet.curiosity = catClamp(catPet.curiosity + 1);
        break;
    case B_TRAINING:
        catPet.intelligence = catClamp(catPet.intelligence + 1);
        catPet.focus = catClamp(catPet.focus + 1);
        break;
    case B_PLAYING:
        catPet.playfulness = catClamp(catPet.playfulness + 1);
        catPet.energy = catClamp(catPet.energy - 1);
        break;
    case B_AFFECTION:
        catPet.affection = catClamp(catPet.affection + 1);
        catPet.sociability = catClamp(catPet.sociability + 1);
        break;
    case B_MISCHIEF:
        catPet.mischievousness = catClamp(catPet.mischievousness + 1);
        break;
    default:
        break;
    }
}

void updateBehavior()
{
    uint32_t now = millis();
    if ((uint32_t)(now - catBehaviorStartedMs) >= catBehaviorDurationMs)
    {
        currentBehavior = selectNextBehavior();
        catBehaviorStartedMs = now;
        catBehaviorDurationMs = random(3000, 10000);
        catCurrentFrame = 0;
        applyBehaviorChange(currentBehavior);
    }
}

void updateGarden()
{
    uint32_t now = millis();
    for (uint8_t i = 0; i < 3; i++)
    {
        Plant &plant = catPet.garden[i];
        if (plant.stage == STAGE_EMPTY || plant.stage == STAGE_DEAD)
            continue;
        uint32_t ticks = (uint32_t)(now - plant.lastUpdate) / 30000UL;
        if (ticks == 0)
            continue;
        plant.lastUpdate += ticks * 30000UL;
        int waterChange = catPet.weather == 2 ? (int)ticks : -(int)ticks;
        plant.water = catClamp((int)plant.water + waterChange);
        int growthChange = plant.water >= 20 && catPet.weather != 3 ? (int)ticks : -(int)ticks;
        plant.growth = catClamp((int)plant.growth + growthChange);
        if (plant.growth == 0 && plant.water == 0)
            plant.stage = STAGE_DEAD;
        else if (plant.water < 20)
            plant.stage = STAGE_WILTING;
        else if (plant.growth > 80)
            plant.stage = STAGE_THRIVING;
        else if (plant.growth > 60)
            plant.stage = STAGE_MATURE;
        else if (plant.growth > 40)
            plant.stage = STAGE_GROWING;
        else if (plant.growth > 20)
            plant.stage = STAGE_YOUNG;
        else
            plant.stage = STAGE_SEEDLING;
    }
}

void updateCatSimulation()
{
    uint32_t now = millis();
    uint32_t rapidTicks = (uint32_t)(now - catLastRapidMs) / 240000UL;
    if (rapidTicks > 0)
    {
        catPet.hunger = catClamp((int)catPet.hunger - (int)rapidTicks);
        catPet.energy = catClamp((int)catPet.energy - (int)rapidTicks);
        catPet.playfulness = catClamp((int)catPet.playfulness - (int)rapidTicks);
        catLastRapidMs += rapidTicks * 240000UL;
    }
    uint32_t mediumTicks = (uint32_t)(now - catLastMediumMs) / 600000UL;
    if (mediumTicks > 0)
    {
        int moodLoss = (catPet.hunger < 25 || catPet.cleanliness < 20) ? 2 : 1;
        if (catPet.weather >= 2)
            moodLoss++;
        catPet.mood = catClamp((int)catPet.mood - moodLoss * (int)mediumTicks);
        catPet.cleanliness = catClamp((int)catPet.cleanliness - (int)mediumTicks);
        catPet.affection = catClamp((int)catPet.affection - (int)mediumTicks);
        catPet.focus = catClamp((int)catPet.focus - (int)mediumTicks);
        catPet.fulfillment = catClamp((int)catPet.fulfillment - (int)mediumTicks);
        catPet.health = calculateCatHealth();
        catLastMediumMs += mediumTicks * 600000UL;
    }
    uint32_t slowTicks = (uint32_t)(now - catLastSlowMs) / 3600000UL;
    if (slowTicks > 0)
    {
        catPet.fitness = catClamp((int)catPet.fitness - (int)slowTicks);
        catPet.serenity = catClamp((int)catPet.serenity - (int)slowTicks);
        catPet.intelligence = catClamp((int)catPet.intelligence - (int)slowTicks);
        catPet.maturity = catClamp((int)catPet.maturity + (int)slowTicks);
        catLastSlowMs += slowTicks * 3600000UL;
    }
    uint32_t verySlowTicks = (uint32_t)(now - catLastVerySlowMs) / 7200000UL;
    if (verySlowTicks > 0)
    {
        catPet.courage = catClamp((int)catPet.courage - (int)verySlowTicks);
        catPet.loyalty = catClamp((int)catPet.loyalty + (int)verySlowTicks);
        catPet.mischievousness = catClamp((int)catPet.mischievousness + (int)verySlowTicks);
        catPet.curiosity = catClamp((int)catPet.curiosity + (int)verySlowTicks);
        catPet.sociability = catClamp((int)catPet.sociability + (int)verySlowTicks);
        catLastVerySlowMs += verySlowTicks * 7200000UL;
    }
    updateGarden();
    uint32_t weatherTicks = (uint32_t)(now - catLastWeatherMs) / 1800000UL;
    if (weatherTicks > 0)
    {
        catPet.weather = (uint8_t)random(0, CAT_WEATHER_COUNT);
        catLastWeatherMs += weatherTicks * 1800000UL;
    }
    updateBehavior();
    if ((uint32_t)(now - catLastSaveMs) >= 300000UL)
        saveCatPet();
}

CatState currentCatState()
{
    if (catPet.energy < 18)
        return CAT_SLEEPING;
    if (catPet.health < 25 || catPet.hunger < 20 || catPet.mood < 20)
        return CAT_SAD;
    if (currentBehavior == B_SLEEPING || currentBehavior == B_NAPPING)
        return CAT_SLEEPING;
    if (currentBehavior == B_SULKING || currentBehavior == B_HIDING)
        return CAT_SAD;
    if (currentBehavior == B_EATING)
        return CAT_EATING;
    if (currentBehavior == B_PLAYING || currentBehavior == B_ZOOMIES)
        return CAT_PLAYING;
    return CAT_IDLE;
}

void drawCatBar(const char *label, uint8_t value, int y)
{
    display.setCursor(2, y);
    display.print(label);
    display.drawRect(48, y, 76, 7, SSD1306_WHITE);
    display.fillRect(50, y + 2, map(value, 0, 100, 0, 72), 3, SSD1306_WHITE);
}

void drawCatIcon(const uint8_t *icon, int x, int y)
{
    static uint8_t paddedIcon[ICON_HEIGHT * 3 + 3];
    for (uint8_t row = 0; row < 17; row++)
        for (uint8_t column = 0; column < 3; column++)
            paddedIcon[row * 3 + column] = pgm_read_byte(icon + row * 3 + column);
    memset(paddedIcon + 17 * 3, 0, 3);
    display.drawBitmap(x, y, paddedIcon, ICON_WIDTH, ICON_HEIGHT, SSD1306_WHITE);
}

void drawCatMetric(const uint8_t *icon, const char *label, uint8_t value, int x, int y)
{
    drawCatIcon(icon, x, y);
    display.setCursor(x + 20, y + 1);
    display.print(label);
    display.setCursor(x + 20, y + 9);
    display.print(value);
    display.print('%');
}

const char *catStatLabel(uint8_t index)
{
    switch (index)
    {
    case 0:
        return "Hunger";
    case 1:
        return "Energy";
    case 2:
        return "Health";
    case 3:
        return "Mood";
    case 4:
        return "Clean";
    case 5:
        return "Love";
    case 6:
        return "Fit";
    case 7:
        return "Calm";
    case 8:
        return "Brave";
    case 9:
        return "Loyal";
    case 10:
        return "Mischief";
    case 11:
        return "Curious";
    case 12:
        return "Social";
    case 13:
        return "Intellect";
    case 14:
        return "Mature";
    case 15:
        return "Fulfill";
    case 16:
        return "Play";
    default:
        return "Focus";
    }
}

const uint8_t *catStatIcon(uint8_t index)
{
    switch (index)
    {
    case 0:
        return icon_food;
    case 1:
        return icon_energy;
    case 2:
        return icon_health;
    case 3:
        return icon_mood;
    case 4:
        return icon_clean;
    case 5:
        return icon_heart;
    case 6:
        return icon_workout;
    case 7:
        return icon_eyes;
    case 8:
        return icon_crown;
    case 9:
        return icon_heart;
    case 10:
        return icon_game;
    case 11:
        return icon_question;
    case 12:
        return icon_paws;
    case 13:
        return icon_education;
    case 14:
        return icon_star;
    case 15:
        return icon_cat_love;
    case 16:
        return icon_game;
    default:
        return icon_question;
    }
}

void startCatBehavior(CatBehavior behavior, uint32_t durationMs = 2500)
{
    currentBehavior = behavior;
    catBehaviorStartedMs = millis();
    catBehaviorDurationMs = durationMs;
    catCurrentFrame = 0;
}

void renderCatHome()
{
    CatState state = currentCatState();
    const Pose *pose = &POSE_SIT_NEU;
    if (state == CAT_SLEEPING)
        pose = &POSE_SLP;
    else if (state == CAT_SAD)
        pose = &POSE_SIT_ANN;
    else if (state == CAT_EATING || state == CAT_PLAYING || catPet.mood > 70)
        pose = &POSE_SIT_HAP;

    uint32_t now = millis();
    if ((uint32_t)(now - catLastFrameUpdate) >= 200)
    {
        catLastFrameUpdate = now;
        catCurrentFrame++;
    }

    clearDisp();
    display.setCursor(2, 1);
    display.print(F("CAT HOME"));
    display.setCursor(64, 1);
    display.print(behaviorName(currentBehavior));
    display.setCursor(111, 1);
    display.print(catWeatherShortName(catPet.weather));
    display.drawFastHLine(0, 10, 128, SSD1306_WHITE);

    int catX = constrain(catWorldX, 12, 100);
    display.drawFastHLine(0, 61, 128, SSD1306_WHITE);
    const uint8_t *bookshelfFrame = spriteFrameData(&BOOKSHELF, 0);
    if (bookshelfFrame != nullptr)
        display.drawBitmap(0, 61 - BOOKSHELF.height, bookshelfFrame,
                           BOOKSHELF.width, BOOKSHELF.height, SSD1306_WHITE);
    const uint8_t *pillowFrame = spriteFrameData(&PILLOW, 0);
    if (pillowFrame != nullptr)
        display.drawBitmap(95, 43, pillowFrame, PILLOW.width, PILLOW.height, SSD1306_WHITE);

    const Sprite *body = &pose->body->sprite;
    const uint8_t *bodyFrame = spriteFrameData(body, catCurrentFrame);
    if (bodyFrame != nullptr)
        display.drawBitmap(catX, 38, bodyFrame, body->width, body->height, SSD1306_WHITE);

    const Sprite *head = pose->head == nullptr ? nullptr : &pose->head->sprite;
    const uint8_t *headFrame = spriteFrameData(head, 0);
    if (headFrame != nullptr)
        display.drawBitmap(catX + pose->body->hx, 38 + pose->body->hy,
                           headFrame, head->width, head->height, SSD1306_WHITE);

    if (pose->tail != nullptr)
    {
        const Sprite *tail = &pose->tail->sprite;
        const uint8_t *tailFrame = spriteFrameData(tail, catCurrentFrame);
        if (tailFrame != nullptr)
            display.drawBitmap(catX + pose->body->tx, 38 + pose->body->ty,
                               tailFrame, tail->width, tail->height, SSD1306_WHITE);
    }

    if (pose->eyes != nullptr && head != nullptr)
    {
        const Sprite *eyes = &pose->eyes->sprite;
        const uint8_t *eyesFrame = spriteFrameData(eyes, catCurrentFrame / 6);
        if (eyesFrame != nullptr)
            display.drawBitmap(catX + pose->body->hx + pose->head->ex,
                               38 + pose->body->hy + pose->head->ey,
                               eyesFrame, eyes->width, eyes->height, SSD1306_WHITE);
    }

    display.fillRect(0, 11, 128, 8, SSD1306_BLACK);
    display.setCursor(2, 12);
    display.print(F("H"));
    display.print(catPet.hunger);
    display.setCursor(42, 12);
    display.print(F("E"));
    display.print(catPet.energy);
    display.setCursor(82, 12);
    display.print(F("M"));
    display.print(catPet.mood);
    drawHint("U:Feed D:Pet U-Hold:Menu");
    display.display();
}

void renderCatMenu()
{
    clearDisp();
    drawMiniHeader("CATODE");
    for (int i = 0; i < (int)ARR_LEN(CAT_MENU_ITEMS); i++)
    {
        int y = 13 + i * 9;
        if (i == catMenuSelection)
        {
            display.fillRect(0, y - 1, 124, 9, SSD1306_WHITE);
            display.setTextColor(SSD1306_BLACK);
        }
        display.setCursor(5, y);
        display.print(i == catMenuSelection ? "> " : "  ");
        display.print(CAT_MENU_ITEMS[i]);
        display.setTextColor(SSD1306_WHITE);
    }
    drawHint("U/D:Choose R:Open L-Hold:Back");
    display.display();
}

void renderCatStats()
{
    clearDisp();
    display.setCursor(2, 1);
    display.print(F("STATS "));
    display.print(catStatsPage + 1);
    display.print(F("/3 U/D"));
    display.drawFastHLine(0, 10, 128, SSD1306_WHITE);
    for (uint8_t slot = 0; slot < 6; slot++)
    {
        uint8_t stat = catStatsPage * 6 + slot;
        int x = (slot % 2) ? 66 : 2;
        int y = 12 + (slot / 2) * 16;
        drawCatMetric(catStatIcon(stat), catStatLabel(stat), catGetStat(stat), x, y);
    }
    display.display();
}

void renderCatStore()
{
    clearDisp();
    drawMiniHeader("STORE");
    display.setCursor(86, 1);
    display.print(F("$"));
    display.print(catPet.coins);
    const uint8_t *storeIcons[] = {icon_food, icon_flower, icon_clean};
    const char *storeLabels[] = {"Food 5c", "Seed 8c", "Water 3c"};
    uint8_t stock[] = {catPet.food, catPet.seeds, catPet.water};
    for (int i = 0; i < (int)ARR_LEN(CAT_STORE_ITEMS); i++)
    {
        int y = 13 + i * 16;
        if (i == catStoreSelection)
            display.drawRect(0, y, 127, 16, SSD1306_WHITE);
        drawCatIcon(storeIcons[i], 2, y);
        display.setCursor(23, y + 5);
        display.print(storeLabels[i]);
        display.setCursor(106, y + 5);
        display.print('x');
        display.print(stock[i]);
    }
    display.display();
}

void renderCatGarden()
{
    clearDisp();
    drawMiniHeader("GARDEN");
    display.setCursor(82, 1);
    display.print(catGardenSelection + 1);
    display.print(F("/3"));
    display.setCursor(99, 1);
    display.print((uint8_t)catPet.selectedSeed + 1);
    for (uint8_t i = 0; i < 3; i++)
    {
        const Plant &plant = catPet.garden[i];
        int x = 12 + i * 40;
        int potWidth = plant.pot == POT_SMALL ? 18 : (plant.pot == POT_MEDIUM ? 22 : (plant.pot == POT_LARGE ? 26 : 30));
        int potX = x + (30 - potWidth) / 2;
        int potY = 36;
        if (i == catGardenSelection)
            display.drawRoundRect(x - 3, 13, 36, 35, 3, SSD1306_WHITE);
        if (plant.stage != STAGE_EMPTY && plant.stage != STAGE_DEAD)
        {
            int height = 3 + plant.growth / 6;
            display.drawLine(x + 15, potY, x + 15, potY - height, SSD1306_WHITE);
            display.drawPixel(x + 15 - (plant.growth > 40 ? 5 : 2), potY - height + 3, SSD1306_WHITE);
            display.drawPixel(x + 15 + (plant.growth > 40 ? 5 : 2), potY - height + 5, SSD1306_WHITE);
            if (plant.stage >= STAGE_MATURE && plant.stage <= STAGE_THRIVING)
                display.drawCircle(x + 15, potY - height - 2, 3, SSD1306_WHITE);
            if (plant.stage == STAGE_WILTING)
                display.drawLine(x + 15, potY - height, x + 20, potY - height + 3, SSD1306_WHITE);
        }
        display.drawRect(potX, potY, potWidth, 10, SSD1306_WHITE);
    }
    const Plant &selected = catPet.garden[catGardenSelection];
    display.setCursor(2, 49);
    display.print(catStageName(selected.stage));
    display.print(' ');
    display.print(selected.water);
    display.print(F("% "));
    display.print(catPotName(selected.pot));
    display.print(' ');
    display.print(catSeedName(catPet.selectedSeed));
    drawHint("U/D:POT R:ACT HOLD:SET");
    display.display();
}

void renderCatMinigames()
{
    clearDisp();
    drawMiniHeader("MINIGAMES");
    const char *gameLabels[] = {"Dino Runner", "Tetris", "Snake"};
    for (int i = 0; i < (int)ARR_LEN(CAT_MINIGAME_ITEMS); i++)
    {
        int y = 13 + i * 16;
        if (i == catMinigameSelection)
            display.drawRect(0, y, 127, 16, SSD1306_WHITE);
        drawCatIcon(icon_game, 2, y);
        display.setCursor(23, y + 5);
        display.print(gameLabels[i]);
    }
    display.display();
}

void runCatApp()
{
    if (!catPetLoaded)
    {
        loadCatPet();
        catPetLoaded = true;
    }
    catScene = CAT_HOME;
    catMenuSelection = 0;
    currentBehavior = B_IDLE;
    catBehaviorStartedMs = millis();
    catBehaviorDurationMs = random(3000, 10000);
    clearBtnFlags();

    while (true)
    {
        Act a = getAction();
        updateCatSimulation();

        if (catScene == CAT_HOME)
        {
            if (a == A_BACK)
            {
                saveCatPet();
                clearBtnFlags();
                return;
            }
            if (a == A_ALT)
                catScene = CAT_MENU;
            if (a == A_UP && catPet.food > 0)
            {
                catPet.food--;
                catPet.hunger = catClamp(catPet.hunger + 25);
                startCatBehavior(B_EATING);
            }
            if (a == A_DOWN)
            {
                catPet.mood = catClamp(catPet.mood + 10);
                catPet.affection = catClamp(catPet.affection + 5);
                catPet.energy = catClamp(catPet.energy - 4);
                catPet.playfulness = catClamp(catPet.playfulness + 5);
                catPet.sociability = catClamp(catPet.sociability + 1);
                startCatBehavior(B_PLAYING);
            }
            if (a == A_LEFT)
                catWorldX = max(12, catWorldX - 4);
            if (a == A_RIGHT)
                catWorldX = min(106, catWorldX + 4);
            renderCatHome();
        }
        else if (catScene == CAT_MENU)
        {
            if (a == A_BACK || a == A_LEFT)
                catScene = CAT_HOME;
            if (a == A_UP)
                catMenuSelection = (catMenuSelection + ARR_LEN(CAT_MENU_ITEMS) - 1) % ARR_LEN(CAT_MENU_ITEMS);
            if (a == A_DOWN)
                catMenuSelection = (catMenuSelection + 1) % ARR_LEN(CAT_MENU_ITEMS);
            if (a == A_RIGHT || a == A_ALT_RIGHT)
            {
                switch (catMenuSelection)
                {
                case 0:
                    catScene = CAT_HOME;
                    break;
                case 1:
                    catScene = CAT_STATS;
                    break;
                case 2:
                    catScene = CAT_STORE;
                    break;
                case 3:
                    catScene = CAT_GARDEN;
                    break;
                case 4:
                    catScene = CAT_MINIGAMES;
                    break;
                }
            }
            renderCatMenu();
        }
        else
        {
            if (a == A_BACK || a == A_LEFT)
                catScene = CAT_MENU;
            if (catScene == CAT_STATS)
            {
                if (a == A_UP)
                    catStatsPage = (catStatsPage + 2) % 3;
                if (a == A_DOWN)
                    catStatsPage = (catStatsPage + 1) % 3;
                renderCatStats();
            }
            else if (catScene == CAT_STORE)
            {
                if (a == A_UP)
                    catStoreSelection = (catStoreSelection + ARR_LEN(CAT_STORE_ITEMS) - 1) % ARR_LEN(CAT_STORE_ITEMS);
                if (a == A_DOWN)
                    catStoreSelection = (catStoreSelection + 1) % ARR_LEN(CAT_STORE_ITEMS);
                if (a == A_RIGHT || a == A_ALT_RIGHT)
                {
                    bool purchased = false;
                    if (catStoreSelection == 0 && catPet.coins >= 5 && catPet.food < 100)
                    {
                        catPet.coins -= 5;
                        catPet.food++;
                        purchased = true;
                    }
                    if (catStoreSelection == 1 && catPet.coins >= 8 && catPet.seeds < 100)
                    {
                        catPet.coins -= 8;
                        catPet.seeds++;
                        purchased = true;
                    }
                    if (catStoreSelection == 2 && catPet.coins >= 3 && catPet.water < 100)
                    {
                        catPet.coins -= 3;
                        catPet.water++;
                        purchased = true;
                    }
                    (void)purchased;
                }
                renderCatStore();
            }
            else if (catScene == CAT_GARDEN)
            {
                if (a == A_UP)
                    catGardenSelection = (catGardenSelection + 2) % 3;
                if (a == A_DOWN)
                    catGardenSelection = (catGardenSelection + 1) % 3;
                if (a == A_ALT)
                    catPet.selectedSeed = (SeedKind)((catPet.selectedSeed + 1) % 4);
                if (a == A_ALT_DOWN)
                    catPet.selectedPot = (PotKind)((catPet.selectedPot + 1) % 4);
                if (a == A_RIGHT)
                {
                    Plant &plant = catPet.garden[catGardenSelection];
                    if ((plant.stage == STAGE_EMPTY || plant.stage == STAGE_DEAD) && catPet.seeds > 0)
                    {
                        catPet.seeds--;
                        plant.pot = catPet.selectedPot;
                        plant.seed = catPet.selectedSeed;
                        plant.stage = STAGE_SEEDLING;
                        plant.water = 50;
                        plant.growth = 1;
                        plant.lastUpdate = millis();
                    }
                    else if (plant.stage != STAGE_EMPTY && plant.stage != STAGE_DEAD && catPet.water > 0)
                    {
                        catPet.water--;
                        plant.water = catClamp(plant.water + 35);
                        plant.lastUpdate = millis();
                    }
                }
                if (a == A_ALT_RIGHT)
                {
                    Plant &plant = catPet.garden[catGardenSelection];
                    if (plant.stage == STAGE_MATURE || plant.stage == STAGE_THRIVING)
                    {
                        catPet.coins = catClamp(catPet.coins + 5);
                        catPet.fulfillment = catClamp(catPet.fulfillment + 3);
                        plant.stage = STAGE_EMPTY;
                        plant.growth = 0;
                        plant.water = 0;
                    }
                }
                renderCatGarden();
            }
            else if (catScene == CAT_MINIGAMES)
            {
                if (a == A_UP)
                    catMinigameSelection = (catMinigameSelection + ARR_LEN(CAT_MINIGAME_ITEMS) - 1) % ARR_LEN(CAT_MINIGAME_ITEMS);
                if (a == A_DOWN)
                    catMinigameSelection = (catMinigameSelection + 1) % ARR_LEN(CAT_MINIGAME_ITEMS);
                if (a == A_RIGHT || a == A_ALT_RIGHT)
                {
                    if (catMinigameSelection == 0)
                        runDino();
                    if (catMinigameSelection == 1)
                        runTetris();
                    if (catMinigameSelection == 2)
                        runSnake();
                    clearBtnFlags();
                    catPet.mood = catClamp(catPet.mood + 5);
                    catPet.energy = catClamp(catPet.energy - 3);
                    catPet.playfulness = catClamp(catPet.playfulness + 3);
                    catPet.coins = catClamp(catPet.coins + 2);
                    startCatBehavior(B_PLAYING);
                }
                renderCatMinigames();
            }
        }
        delay(20);
    }
}

// ============================================================
//  APPS / GAMES DISPATCHERS
// ============================================================
const char *APPS_ITEMS[] = {"Vault", "Profile", "Life", "Screensaver", "Timer", "Sleep",
                             "Cat Pet", "Adopt", "Forecast", "Treehouse", "Outside"};
#define APPS_COUNT ARR_LEN(APPS_ITEMS)

void runTimer();
void runSleepMode();

void runAppsMenu()
{
    int r = runMenu("APPS", APPS_ITEMS, APPS_COUNT, 0);
    if (r == -1)
        return;
    clearBtnFlags();
    switch (r)
    {
    case 0:
        runVault();
        break;
    case 1:
        runProfile();
        break;
    case 2:
        runLife();
        break;
    case 3:
        runScreensaver();
        break;
    case 4:
        runTimer();
        break;
    case 5:
        runSleepMode();
        break;
    case 6:
        runCatApp();
        break;
    case 7:
        runAdoptionScene();
        break;
    case 8:
        runForecastScene();
        break;
    case 9:
        runTreehouseScene();
        break;
    case 10:
        runOutsideScene();
        break;
    }
    clearBtnFlags();
}

const char *GAMES_ITEMS[] = {"Dino Runner", "Tetris", "Snake"};
#define GAMES_COUNT ARR_LEN(GAMES_ITEMS)

void runGamesMenu()
{
    int r = runMenu("GAMES", GAMES_ITEMS, GAMES_COUNT, 0);
    if (r == -1)
        return;
    clearBtnFlags();
    switch (r)
    {
    case 0:
        runDino();
        break;
    case 1:
        runTetris();
        break;
    case 2:
        runSnake();
        break;
    }
    clearBtnFlags();
}

// ============================================================
//  TIMER
// ============================================================
uint16_t TIMER_PRESETS[] = {300, 900, 1500, 2700, 3600};
const char *TIMER_NAMES[] = {"5m Break", "15m Quick", "25m Study", "45m Lecture", "60m Exam"};
#define TIMER_COUNT ARR_LEN(TIMER_PRESETS)

void runTimer()
{
    clearBtnFlags();
    int presetIdx = 2;
    while (true)
    {
        clearDisp();
        drawMiniHeader("TIMER");
        display.setCursor(10, 22);
        display.print(TIMER_NAMES[presetIdx]);
        display.setTextSize(2);
        display.setCursor(44, 36);
        display.print(TIMER_PRESETS[presetIdx] / 60);
        display.setTextSize(1);
        display.setCursor(76, 42);
        display.print("min");
        display.display();
        Act a = getAction();
        if (a == A_UP)
            presetIdx = (presetIdx + 1) % TIMER_COUNT;
        if (a == A_DOWN)
            presetIdx = (presetIdx - 1 + TIMER_COUNT) % TIMER_COUNT;
        if (a == A_BACK)
        {
            clearBtnFlags();
            return;
        }
        if (a == A_RIGHT)
        {
            uint32_t total = TIMER_PRESETS[presetIdx], rem = total;
            unsigned long tick = millis();
            while (rem > 0)
            {
                Act a2 = getAction();
                if (a2 == A_BACK)
                {
                    clearBtnFlags();
                    return;
                }
                if (millis() - tick >= 1000)
                {
                    tick = millis();
                    rem--;
                }
                clearDisp();
                drawMiniHeader("TIMER");
                display.setTextSize(2);
                display.setCursor(38, 18);
                int m = rem / 60, s = rem % 60;
                if (m < 10)
                    display.print('0');
                display.print(m);
                display.print(':');
                if (s < 10)
                    display.print('0');
                display.print(s);
                int cx = 64, cy = 48, radius = 10;
                display.drawCircle(cx, cy, radius, SSD1306_WHITE);
                float progress = (float)rem / total;
                float endAngle = -PI / 2 + progress * 2 * PI;
                float angle = -PI / 2;
                while (angle < endAngle)
                {
                    display.drawPixel(cx + cos(angle) * radius, cy + sin(angle) * radius, SSD1306_WHITE);
                    display.drawPixel(cx + cos(angle) * (radius - 1), cy + sin(angle) * (radius - 1), SSD1306_WHITE);
                    angle += 0.15f;
                }
                display.display();
                delay(30);
            }
            for (int i = 0; i < 6; i++)
            {
                display.invertDisplay(true);
                delay(50);
                display.invertDisplay(false);
                delay(50);
            }
            clearDisp();
            display.setTextSize(2);
            display.setCursor(14, 26);
            display.print(F("TIME UP"));
            display.display();
            delay(50);
        }
        delay(30);
    }
}

// ============================================================
//  SETTINGS SCREENS
// ============================================================
void setBrightnessScreen()
{
    clearBtnFlags();
    const char *lvls[] = {"25%", "50%", "75%", "100%"};
    while (true)
    {
        clearDisp();
        drawMiniHeader("Brightness");
        for (int i = 0; i < (int)ARR_LEN(lvls); i++)
        {
            int y = 18 + i * 9;
            if (i == cfg.brightness)
            {
                display.fillRect(0, y - 1, 100, 9, SSD1306_WHITE);
                display.setTextColor(SSD1306_BLACK);
            }
            display.setCursor(6, y);
            display.print(lvls[i]);
            display.setTextColor(SSD1306_WHITE);
        }
        display.display();
        Act a = getAction();
        bool changed = false;
        if (a == A_UP)
        {
            cfg.brightness = (cfg.brightness - 1 + ARR_LEN(lvls)) % ARR_LEN(lvls);
            changed = true;
        }
        if (a == A_DOWN)
        {
            cfg.brightness = (cfg.brightness + 1) % ARR_LEN(lvls);
            changed = true;
        }
        if (changed)
            applyBrightness();
        if (a == A_RIGHT)
        {
            saveCfg();
            clearBtnFlags();
            return;
        }
        if (a == A_BACK)
        {
            saveCfg();
            clearBtnFlags();
            return;
        }
        delay(30);
    }
}
void setTimeoutScreen()
{
    clearBtnFlags();
    uint32_t TO[] = {5000, 10000, 20000, 30000, 60000, 0};
    const char *TN[] = {"5s", "10s", "20s", "30s", "60s", "Never"};
    int count = ARR_LEN(TO), sel = 0;
    for (int i = 0; i < count; i++)
        if (TO[i] == cfg.menuTimeoutMs)
            sel = i;
    while (true)
    {
        clearDisp();
        drawMiniHeader("Timeout");
        for (int i = 0; i < count; i++)
        {
            int y = 14 + i * 7;
            if (i == sel)
            {
                display.fillRect(0, y - 1, 100, 7, SSD1306_WHITE);
                display.setTextColor(SSD1306_BLACK);
            }
            display.setCursor(6, y);
            display.print(TN[i]);
            display.setTextColor(SSD1306_WHITE);
        }
        display.display();
        Act a = getAction();
        if (a == A_UP)
            sel = (sel - 1 + count) % count;
        if (a == A_DOWN)
            sel = (sel + 1) % count;
        if (a == A_RIGHT)
        {
            cfg.menuTimeoutMs = TO[sel];
            saveCfg();
            clearBtnFlags();
            return;
        }
        if (a == A_BACK)
        {
            clearBtnFlags();
            return;
        }
        delay(30);
    }
}
void setWallpaperScreen()
{
    clearBtnFlags();
    int sel = cfg.wallpaperIdx + 1;
    if (sel < 0)
        sel = 0;
    if (sel >= WALLPAPER_COUNT)
        sel = WALLPAPER_COUNT - 1;
    while (true)
    {
        clearDisp();
        drawMiniHeader("Wallpaper");
        for (int i = 0; i < WALLPAPER_COUNT; i++)
        {
            int y = 16 + i * 11;
            if (i == sel)
            {
                display.fillRect(0, y - 1, 128, 10, SSD1306_WHITE);
                display.setTextColor(SSD1306_BLACK);
            }
            display.setCursor(6, y);
            display.print(WALLPAPER_NAMES[i]);
            display.setTextColor(SSD1306_WHITE);
        }
        display.setCursor(0, 56);
        display.print(F("R:Preview L-Hold:Save"));
        display.display();

        Act a = getAction();
        if (a == A_UP)
            sel = (sel - 1 + WALLPAPER_COUNT) % WALLPAPER_COUNT;
        if (a == A_DOWN)
            sel = (sel + 1) % WALLPAPER_COUNT;
        if (a == A_RIGHT)
        {
            int frame = 0;
            unsigned long lastF = millis();
            renderWallpaperFrame(sel, 0);
            while (true)
            {
                Act ap = getAction();
                if (ap != A_NONE)
                    break;
                if (millis() - lastF >= 150)
                {
                    lastF = millis();
                    frame++;
                    renderWallpaperFrame(sel, frame);
                }
                delay(10);
            }
            clearBtnFlags();
        }
        if (a == A_BACK)
        {
            cfg.wallpaperIdx = (int8_t)sel;
            saveCfg();
            clearBtnFlags();
            return;
        }
        delay(30);
    }
}
void setScreenFlipScreen()
{
    clearBtnFlags();
    while (true)
    {
        clearDisp();
        drawMiniHeader("Screen Flip");
        display.setCursor(0, 22);
        display.print(F("Flip 180:"));
        display.setTextSize(2);
        display.setCursor(20, 34);
        display.print(cfg.screenFlip ? F("ON") : F("OFF"));
        display.setTextSize(1);
        display.display();
        Act a = getAction();
        if (a == A_RIGHT)
        {
            cfg.screenFlip = !cfg.screenFlip;
            applyScreenFlip();
            saveCfg();
        }
        if (a == A_BACK)
        {
            saveCfg();
            clearBtnFlags();
            return;
        }
        delay(30);
    }
}
void resetAllScreen()
{
    clearBtnFlags();
    clearDisp();
    drawMiniHeader("Reset All");
    display.setCursor(4, 20);
    display.println(F("Hold RIGHT 3s"));
    display.setCursor(4, 32);
    display.println(F("to confirm"));
    display.setCursor(4, 48);
    display.println(F("Long LEFT: Cancel"));
    display.display();
    unsigned long holdStart = 0;
    bool holding = false;
    while (true)
    {
        Act a = getAction();
        if (a == A_BACK)
        {
            clearBtnFlags();
            return;
        }
        bool rightDown = (btn[BI_RIGHT].stable == LOW);
        if (rightDown)
        {
            if (!holding)
            {
                holding = true;
                holdStart = millis();
            }
            unsigned long held = millis() - holdStart;
            clearDisp();
            drawMiniHeader("Reset All");
            display.setCursor(4, 20);
            display.println(F("Keep holding..."));
            int w = constrain((int)map(held, 0, 3000, 0, 116), 0, 116);
            display.drawRect(6, 40, 116, 10, SSD1306_WHITE);
            display.fillRect(8, 42, w, 6, SSD1306_WHITE);
            display.display();
            if (held >= 3000)
            {
                prefs.begin("neon", false);
                prefs.clear();
                prefs.end();
                prefs.begin("trex", false);
                prefs.clear();
                prefs.end();
                prefs.begin("snake", false);
                prefs.clear();
                prefs.end();
                prefs.begin("catpet", false);
                prefs.clear();
                prefs.end();
                clearDisp();
                display.setTextSize(2);
                display.setCursor(10, 26);
                display.print(F("DONE"));
                display.display();
                delay(50);
                ESP.restart();
            }
        }
        else
            holding = false;
        delay(20);
    }
}
void aboutScreen()
{
    clearBtnFlags();
    clearDisp();
    drawMiniHeader("About");
    display.setCursor(2, 18);
    display.println(F("NEON OS v3.3"));
    display.setCursor(2, 30);
    display.println(F("Apps - Games - Settings"));
    display.setCursor(2, 42);
    display.println(F("13 quotes - 3 games"));
    display.display();
    while (getAction() != A_BACK)
        delay(50);
    clearBtnFlags();
}

const char *SETTINGS_ITEMS[] = {"Brightness", "Timeout", "Wallpaper", "Screen Flip", "Reset All", "About"};
#define SET_COUNT ARR_LEN(SETTINGS_ITEMS)
void runSettings()
{
    int r = runMenu("SETTINGS", SETTINGS_ITEMS, SET_COUNT, 0);
    if (r == -1)
        return;
    clearBtnFlags();
    switch (r)
    {
    case 0:
        setBrightnessScreen();
        break;
    case 1:
        setTimeoutScreen();
        break;
    case 2:
        setWallpaperScreen();
        break;
    case 3:
        setScreenFlipScreen();
        break;
    case 4:
        resetAllScreen();
        break;
    case 5:
        aboutScreen();
        break;
    }
    clearBtnFlags();
}

// ============================================================
//  SLEEP + IDLE
// ============================================================
void runSleepMode()
{
    for (int i = 0; i < 3; i++)
    {
        display.invertDisplay(true);
        delay(30);
        display.invertDisplay(false);
        delay(30);
    }
    clearDisp();
    display.display();
    display.ssd1306_command(SSD1306_DISPLAYOFF);
    while (getAction() == A_NONE)
        delay(50);
    display.ssd1306_command(SSD1306_DISPLAYON);
    applyBrightness();
    clearBtnFlags();
}
void checkIdleSleep()
{
    if (cfg.menuTimeoutMs > 0 && (millis() - lastActivity) > cfg.menuTimeoutMs)
    {
        runSleepMode();
        lastActivity = millis();
    }
}

// ============================================================
//  SETUP
// ============================================================
void setup()
{
    Serial.begin(115200);
    for (int i = 0; i < 4; i++)
        pinMode(BTN_PINS[i], INPUT_PULLUP);
    Wire.begin(SDA_PIN, SCL_PIN);
    Wire.setClock(400000);
    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR))
    {
        Serial.println("OLED FAILED");
        while (true)
            delay(50);
    }
    display.cp437(true);
    display.clearDisplay();
    display.display();
    loadCfg();
    applyBrightness();
    applyScreenFlip();
    randomSeed(esp_random());
    runBootScreen();

    // Auto-trigger adoption scene on first boot (before wallpaper)
    prefs.begin("catpet", true);
    bool catAdopted = prefs.getBool("adopted", false);
    prefs.end();
    if (!catAdopted) runAdoptionScene();

    if (cfg.wallpaperIdx >= 0)
    {
        unsigned long start = millis();
        int f = 0;
        unsigned long lastF = millis();
        while (millis() - start < 1200)
        {
            if (millis() - lastF >= 150)
            {
                lastF = millis();
                f++;
            }
            renderWallpaperFrame(cfg.wallpaperIdx, f);
            delay(20);
        }
    }

    homeSel = 0;
    homeAngle = 0.0f;
    homeAngleVel = 0.0f;
    lastActivity = millis();
    clearBtnFlags();
}

// ============================================================
//  MAIN LOOP
// ============================================================
void loop()
{
    Act a = getAction();
    if (a == A_DOWN)
        setHomeSel(homeSel + 1);
    if (a == A_UP)
        setHomeSel(homeSel - 1);
    if (a == A_RIGHT)
    {
        int r = homeSel;
        clearBtnFlags();
        switch (r)
        {
        case 0:
            runAppsMenu();
            break;
        case 1:
            runGamesMenu();
            break;
        case 2:
            runSettings();
            break;
        }
        clearBtnFlags();
    }
    updateOrbitAnimation();
    renderHome();
    checkIdleSleep();
    delay(16);
}