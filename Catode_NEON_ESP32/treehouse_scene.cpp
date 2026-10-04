// ============================================================
//  TREEHOUSE SCENE — Indoor location with wooden platform
//  Ported from Rust: treehouse_scene.rs
// ============================================================

#include "neon_scene_api.h"
#include "generated_assets.h"

static int treehouseCameraX = 0;
static int treehouseWorldX = 64;
static const int TREEHOUSE_WORLD_WIDTH = 256;

static void treehouseDraw() {
    clearDisp();
    // Header
    display.setCursor(2, 1);
    display.print(F("TREEHOUSE"));
    display.drawFastHLine(0, 10, 128, SSD1306_WHITE);

    // Background sky
    display.drawFastHLine(0, 20, 128, SSD1306_WHITE);
    display.drawFastHLine(0, 25, 128, SSD1306_WHITE);
    display.drawFastHLine(0, 30, 128, SSD1306_WHITE);

    // Midground platform (wooden floor)
    int mg_offset = treehouseCameraX;
    int mgSx = max(0, -mg_offset);
    int mgEx = min(128, 300 - mg_offset);
    if (mgSx < mgEx) {
        display.drawLine(mgSx, 57, mgEx, 57, SSD1306_WHITE);
        // Vertical posts
        int posts[] = {20, 90, 160, 230};
        for (uint8_t i = 0; i < 4; i++) {
            int px = posts[i] - mg_offset;
            if (mgSx <= px && px <= mgEx) {
                display.drawLine(px, 57, px, 59, SSD1306_WHITE);
            }
        }
    }

    // Tree trunks (framing)
    display.fillRect(-mg_offset - 1, 0, 10, 61, SSD1306_WHITE);
    display.fillRect(180 - mg_offset, 0, 10, 61, SSD1306_WHITE);
    display.drawRect(-mg_offset - 1, 0, 10, 61, SSD1306_WHITE);
    display.drawRect(180 - mg_offset, 0, 10, 61, SSD1306_WHITE);

    // Foreground platform
    int fgSx = max(0, -treehouseCameraX);
    int fgEx = min(128, TREEHOUSE_WORLD_WIDTH - treehouseCameraX);
    if (fgSx < fgEx) {
        display.drawLine(fgSx, 59, fgEx, 59, SSD1306_WHITE);
        display.drawLine(fgSx, 61, fgEx, 61, SSD1306_WHITE);
        // Posts
        int fgPosts[] = {10, 80, 160, 245};
        for (uint8_t i = 0; i < 4; i++) {
            int px = fgPosts[i] - treehouseCameraX;
            if (fgSx <= px && px <= fgEx) {
                display.drawLine(px, 59, px, 63, SSD1306_WHITE);
            }
        }
    }

    // Draw cat on the platform — animated
    int catScreenX = treehouseWorldX - treehouseCameraX;
    unsigned long now = millis();

    static uint8_t thTailFrame = 0;
    static uint8_t thEyeFrame  = 0;
    static uint8_t thHeadFrame = 0;
    static unsigned long thTailT = 0;
    static unsigned long thEyeT  = 0;
    static unsigned long thHeadT = 0;

    if (now - thTailT >= 80)  { thTailT = now; thTailFrame = (thTailFrame + 1) % TAIL_NEUTRAL_FRAMES_COUNT; }
    if (now - thEyeT  >= 200) { thEyeT  = now; thEyeFrame  = (thEyeFrame  + 1) % EYES_SIDE_NEUTRAL_FRAMES_COUNT; }
    if (now - thHeadT >= 300) { thHeadT = now; thHeadFrame = (thHeadFrame + 1) % HEAD_SIDE_NOM_FRAMES_COUNT; }

    // Body (sitting — 1 frame)
    display.drawBitmap(catScreenX,      38, BODY_SIDE_SITTING_FRAMES[0],          21, 19, SSD1306_WHITE);
    // Animated tail
    display.drawBitmap(catScreenX + 8,  37, TAIL_NEUTRAL_FRAMES[thTailFrame],     14, 21, SSD1306_WHITE);
    // Head with gentle nod
    display.drawBitmap(catScreenX - 4,  23, HEAD_SIDE_NOM_FRAMES[thHeadFrame],    17, 24, SSD1306_WHITE);
    // Blinking eyes over head
    display.drawBitmap(catScreenX - 4 + 3, 23 + 10, EYES_SIDE_NEUTRAL_FRAMES[thEyeFrame], 14, 3, SSD1306_WHITE);

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

    display.setCursor(2, 57);
    display.print(F("L/R:Move  LH:Back"));
    display.display();
}

void runTreehouseScene() {
    treehouseWorldX = 64;
    treehouseCameraX = 0;
    clearBtnFlags();
    while (true) {
        Act a = getAction();
        if (a == A_LEFT)  treehouseWorldX = max(20, treehouseWorldX - 4);
        if (a == A_RIGHT) treehouseWorldX = min(236, treehouseWorldX + 4);
        if (a == A_BACK || a == A_ALT) {
            clearBtnFlags();
            return;
        }
        treehouseCameraX = treehouseWorldX - 64;
        if (treehouseCameraX < 0) treehouseCameraX = 0;
        if (treehouseCameraX > TREEHOUSE_WORLD_WIDTH - 128)
            treehouseCameraX = TREEHOUSE_WORLD_WIDTH - 128;
        treehouseDraw();
        delay(20);
    }
}