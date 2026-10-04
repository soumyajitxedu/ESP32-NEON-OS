// ============================================================
//  OUTSIDE SCENE — Outdoor yard with grass and weather
//  Ported from Rust: outside_scene.rs (simplified)
// ============================================================

#include "neon_scene_api.h"
#include "generated_assets.h"

struct OutsideCritter {
    float x;
    float y;
    float vx;
    uint8_t frame;
    uint8_t active;
};

static OutsideCritter outsideCritters[4];
static int outsideCameraX = 0;
static int outsideWorldX = 64;
static const int OUTSIDE_WORLD_WIDTH = 256;
static const int OUTSIDE_GRASS_XS[] = {10, 35, 80, 110, 150, 190, 230};

static void outsideSpawnCritters() {
    for (uint8_t i = 0; i < 4; i++) {
        outsideCritters[i].x = random(20, OUTSIDE_WORLD_WIDTH - 20);
        outsideCritters[i].y = random(15, 45);
        outsideCritters[i].vx = (random(0, 2) == 0) ? -0.5f : 0.5f;
        outsideCritters[i].frame = 0;
        outsideCritters[i].active = 1;
    }
}

static void outsideUpdateCritters() {
    for (uint8_t i = 0; i < 4; i++) {
        if (!outsideCritters[i].active) continue;
        outsideCritters[i].x += outsideCritters[i].vx;
        outsideCritters[i].frame = (outsideCritters[i].frame + 1) % 2;
        if (outsideCritters[i].x < 0 || outsideCritters[i].x > OUTSIDE_WORLD_WIDTH) {
            outsideCritters[i].active = 0;
        }
    }
}

static void outsideDraw() {
    clearDisp();
    // Header
    display.setCursor(2, 1);
    display.print(F("OUTSIDE"));
    display.setCursor(80, 1);
    display.print(catWeatherShortName(catPet.weather));
    display.drawFastHLine(0, 10, 128, SSD1306_WHITE);

    // Sky
    display.drawFastHLine(0, 20, 128, SSD1306_WHITE);
    display.drawFastHLine(0, 28, 128, SSD1306_WHITE);

    // Sun or clouds
    display.drawCircle(110, 20, 6, SSD1306_WHITE);

    // Ground
    display.drawFastHLine(0, 56, 128, SSD1306_WHITE);
    display.drawFastHLine(0, 57, 128, SSD1306_WHITE);

    // Grass tufts
    for (uint8_t i = 0; i < 7; i++) {
        int sx = OUTSIDE_GRASS_XS[i] - outsideCameraX;
        if (sx < -5 || sx > 128 + 5) continue;
        display.drawLine(sx, 56, sx - 2, 52, SSD1306_WHITE);
        display.drawLine(sx, 56, sx, 52, SSD1306_WHITE);
        display.drawLine(sx, 56, sx + 2, 52, SSD1306_WHITE);
    }

    // Critters
    for (uint8_t i = 0; i < 4; i++) {
        if (!outsideCritters[i].active) continue;
        int cx = (int)outsideCritters[i].x - outsideCameraX;
        if (cx < -5 || cx > 128 + 5) continue;
        int cy = (int)outsideCritters[i].y;
        if (outsideCritters[i].frame) {
            display.drawPixel(cx, cy, SSD1306_WHITE);
            display.drawPixel(cx + 1, cy - 1, SSD1306_WHITE);
            display.drawPixel(cx + 2, cy, SSD1306_WHITE);
        } else {
            display.drawPixel(cx, cy - 1, SSD1306_WHITE);
            display.drawPixel(cx + 1, cy, SSD1306_WHITE);
            display.drawPixel(cx + 2, cy - 1, SSD1306_WHITE);
        }
    }

    // Weather effect (rain)
    if (catPet.weather == 2) {
        for (uint8_t i = 0; i < 8; i++) {
            int rx = (i * 17) % 128;
            int ry = (millis() / 50 + i * 7) % 56;
            display.drawLine(rx, ry, rx - 1, ry + 3, SSD1306_WHITE);
        }
    }

    // Cat — animated
    int catScreenX = outsideWorldX - outsideCameraX;
    unsigned long now = millis();

    static uint8_t outBodyFrame = 0;
    static uint8_t outTailFrame = 0;
    static uint8_t outEyeFrame  = 0;
    static uint8_t outHeadFrame = 0;
    static unsigned long outBodyT = 0;
    static unsigned long outTailT = 0;
    static unsigned long outEyeT  = 0;
    static unsigned long outHeadT = 0;
    static bool outWasMoving = false;
    static int  outLastWorldX = 0;

    bool outMoving = (outsideWorldX != outLastWorldX);
    outLastWorldX  = outsideWorldX;

    if (outMoving) {
        // Walking animation: cycle BODY_SIDE_WALKING frames every 100 ms
        if (now - outBodyT >= 100) {
            outBodyT = now;
            outBodyFrame = (outBodyFrame + 1) % BODY_SIDE_WALKING_FRAMES_COUNT;
        }
        display.drawBitmap(catScreenX,     38, BODY_SIDE_WALKING_FRAMES[outBodyFrame], 21, 19, SSD1306_WHITE);
        display.drawBitmap(catScreenX - 4, 24, HEAD_SIDE_NEUTRAL_FRAMES[0],           17, 24, SSD1306_WHITE);
    } else {
        // Idle: animated tail, blinking eyes, gentle head nod
        if (now - outTailT >= 80)  { outTailT = now; outTailFrame = (outTailFrame + 1) % TAIL_NEUTRAL_FRAMES_COUNT; }
        if (now - outEyeT  >= 200) { outEyeT  = now; outEyeFrame  = (outEyeFrame  + 1) % EYES_SIDE_NEUTRAL_FRAMES_COUNT; }
        if (now - outHeadT >= 300) { outHeadT = now; outHeadFrame = (outHeadFrame + 1) % HEAD_SIDE_NOM_FRAMES_COUNT; }

        display.drawBitmap(catScreenX,          38, BODY_SIDE_SITTING_FRAMES[0],             21, 19, SSD1306_WHITE);
        display.drawBitmap(catScreenX + 8,      37, TAIL_NEUTRAL_FRAMES[outTailFrame],        14, 21, SSD1306_WHITE);
        display.drawBitmap(catScreenX - 4,      24, HEAD_SIDE_NOM_FRAMES[outHeadFrame],       17, 24, SSD1306_WHITE);
        display.drawBitmap(catScreenX - 4 + 3,  34, EYES_SIDE_NEUTRAL_FRAMES[outEyeFrame],   14,  3, SSD1306_WHITE);
    }

    // HUD
    display.setCursor(2, 12);
    display.print(F("H"));
    display.print(catPet.hunger);
    display.setCursor(42, 12);
    display.print(F("E"));
    display.print(catPet.energy);
    display.setCursor(82, 12);
    display.print(F("M"));
    display.print(catPet.mood);

    display.setCursor(2, 60);
    display.print(F("L/R:Move  LH:Back"));
    display.display();
}

void runOutsideScene() {
    outsideWorldX = 64;
    outsideCameraX = 0;
    outsideSpawnCritters();
    clearBtnFlags();
    while (true) {
        Act a = getAction();
        if (a == A_LEFT)  outsideWorldX = max(12, outsideWorldX - 4);
        if (a == A_RIGHT) outsideWorldX = min(OUTSIDE_WORLD_WIDTH - 12, outsideWorldX + 4);
        if (a == A_BACK || a == A_ALT) {
            clearBtnFlags();
            return;
        }
        outsideCameraX = outsideWorldX - 64;
        if (outsideCameraX < 0) outsideCameraX = 0;
        if (outsideCameraX > OUTSIDE_WORLD_WIDTH - 128)
            outsideCameraX = OUTSIDE_WORLD_WIDTH - 128;
        outsideUpdateCritters();
        outsideDraw();
        delay(50);
    }
}