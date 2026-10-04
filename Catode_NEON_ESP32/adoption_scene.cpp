// ============================================================
//  ADOPTION SCENE — First-run cat selection
//  Ported from Rust: adoption_scene.rs
// ============================================================

#include "neon_scene_api.h"
#include "generated_assets.h"

enum AdoptState {
    ADOPT_GRID,
    ADOPT_PROFILE,
    ADOPT_CONFIRM,
    ADOPT_MOMENT
};

enum AdoptMomentPhase {
    ADOPT_WALKING,
    ADOPT_SITTING
};

struct AdoptCandidate {
    uint32_t seed;
    uint8_t hunger;
    uint8_t energy;
    uint8_t health;
    uint8_t mood;
    uint8_t cleanliness;
    uint8_t affection;
    uint8_t fitness;
    uint8_t serenity;
    uint8_t courage;
    uint8_t loyalty;
    uint8_t mischievousness;
    uint8_t curiosity;
    uint8_t sociability;
    uint8_t intelligence;
    uint8_t maturity;
    uint8_t fulfillment;
    uint8_t playfulness;
    uint8_t focus;
    uint8_t gender; // 0 = Tom, 1 = Queen
    uint8_t starSign;
    const char* name;
};

static AdoptCandidate adoptCandidates[4];
static uint8_t adoptSelected = 0;
static uint8_t adoptViewing = 0;
static AdoptState adoptState = ADOPT_GRID;
static AdoptMomentPhase adoptMomentPhase = ADOPT_WALKING;
static float adoptMomentX = -20.0f;
static float adoptMomentTimer = 0.0f;
static float adoptBubbleProg = 0.0f;

static const char* TOM_NAMES[] = {"Milo", "Oscar", "Leo", "Felix"};
static const char* QUEEN_NAMES[] = {"Luna", "Cleo", "Bella", "Nala"};

static const char* starSignName(uint8_t idx) {
    static const char* signs[] = {
        "Aries", "Taurus", "Gemini", "Cancer", "Leo", "Virgo",
        "Libra", "Scorpio", "Sagittarius", "Capricorn", "Aquarius", "Pisces"
    };
    return signs[idx % 12];
}

static void adoptGenerateCandidates() {
    for (uint8_t i = 0; i < 4; i++) {
        AdoptCandidate& c = adoptCandidates[i];
        c.seed = random(2147483647);
        c.hunger = 60 + random(0, 40);
        c.energy = 60 + random(0, 40);
        c.health = 80 + random(0, 20);
        c.mood = 50 + random(0, 40);
        c.cleanliness = 70 + random(0, 30);
        c.affection = 30 + random(0, 40);
        c.fitness = 50 + random(0, 40);
        c.serenity = 40 + random(0, 40);
        c.courage = 30 + random(0, 50);
        c.loyalty = 40 + random(0, 50);
        c.mischievousness = 20 + random(0, 50);
        c.curiosity = 50 + random(0, 50);
        c.sociability = 30 + random(0, 50);
        c.intelligence = 30 + random(0, 50);
        c.maturity = 10 + random(0, 40);
        c.fulfillment = 40 + random(0, 40);
        c.playfulness = 50 + random(0, 50);
        c.focus = 40 + random(0, 40);
        c.gender = (i % 2 == 0) ? 0 : 1;
        c.starSign = c.seed % 12;
        if (c.gender == 0) {
            c.name = TOM_NAMES[random(0, 4)];
        } else {
            c.name = QUEEN_NAMES[random(0, 4)];
        }
    }
}

static void adoptDrawGrid() {
    clearDisp();
    display.setCursor(24, 1);
    display.print(F("Adoptable Pets"));
    display.drawFastHLine(0, 10, 128, SSD1306_WHITE);
    const int CELL_W = 64, CELL_H = 27;
    const int TITLE_H = 11;
    for (uint8_t i = 0; i < 4; i++) {
        int col = i % 2;
        int row = i / 2;
        int cx = col * CELL_W;
        int cy = TITLE_H + row * CELL_H;
        // Gender icon in the corner
        const uint8_t* genderIcon = (adoptCandidates[i].gender == 0) ? icon_paws : icon_heart;
        drawCatIcon(genderIcon, cx + 2, cy + 2);
        // Name
        display.setCursor(cx + 2, cy + CELL_H - 10);
        display.print(adoptCandidates[i].name);
        // Selection frame
        if (i == adoptSelected) {
            display.drawRect(cx, cy, CELL_W - 1, CELL_H - 1, SSD1306_WHITE);
            display.drawRect(cx + 1, cy + 1, CELL_W - 3, CELL_H - 3, SSD1306_WHITE);
        } else {
            display.drawRect(cx, cy, CELL_W - 1, CELL_H - 1, SSD1306_WHITE);
        }
    }
    display.setCursor(0, 57);
    display.print(F("A:Inspect  LH:Back"));
    display.display();
}

static void adoptDrawProfile() {
    clearDisp();
    const AdoptCandidate& c = adoptCandidates[adoptViewing];
    display.setCursor(2, 1);
    display.print(c.name);
    display.drawFastHLine(0, 10, 128, SSD1306_WHITE);
    display.setCursor(2, 13);
    display.print(F("Sign: "));
    display.print(starSignName(c.starSign));
    display.setCursor(2, 22);
    display.print(F("Gender: "));
    display.print(c.gender == 0 ? F("Tom") : F("Queen"));
    display.setCursor(2, 31);
    display.print(F("Mood: "));
    display.print(c.mood);
    display.print(F("%  Energy: "));
    display.print(c.energy);
    display.print(F("%"));
    display.setCursor(2, 40);
    display.print(F("Courage: "));
    display.print(c.courage);
    display.print(F("%  Love: "));
    display.print(c.loyalty);
    display.print(F("%"));
    display.setCursor(0, 57);
    display.print(F("A:Adopt  B:Back"));
    display.display();
}

static void adoptDrawConfirm() {
    clearDisp();
    display.drawRect(4, 12, 120, 40, SSD1306_WHITE);
    display.fillRect(5, 13, 118, 38, SSD1306_BLACK);
    display.setCursor(8, 16);
    display.print(F("Adopt this cat?"));
    display.setCursor(8, 26);
    display.print(adoptCandidates[adoptViewing].name);
    display.setCursor(20, 40);
    display.print(F("A:Yes   B:No"));
    display.display();
}

static const uint8_t EXCLAIM[] PROGMEM = {
    0x70, 0x70, 0x20, 0x20, 0x00, 0x20
};
#define EXCLAIM_W 8
#define EXCLAIM_H 6

// Animation state for adoption moment
static uint8_t adoptWalkFrame   = 0;
static uint8_t adoptTailFrame   = 0;
static uint8_t adoptEyeFrame    = 0;
static unsigned long adoptWalkT = 0;
static unsigned long adoptTailT = 0;
static unsigned long adoptEyeT  = 0;

static void adoptDrawMoment() {
    clearDisp();
    display.drawFastHLine(0, 56, 128, SSD1306_WHITE);
    int catX = (int)adoptMomentX;
    unsigned long now = millis();

    if (adoptMomentPhase == ADOPT_WALKING) {
        // Cycle walk frames every 100 ms
        if (now - adoptWalkT >= 100) {
            adoptWalkT = now;
            adoptWalkFrame = (adoptWalkFrame + 1) % BODY_SIDE_WALKING_FRAMES_COUNT;
        }
        display.drawBitmap(catX, 37, BODY_SIDE_WALKING_FRAMES[adoptWalkFrame], 21, 19, SSD1306_WHITE);
        display.drawBitmap(catX - 4, 23, HEAD_SIDE_NEUTRAL_FRAMES[0], 17, 24, SSD1306_WHITE);
    } else {
        // Animate tail every 80 ms
        if (now - adoptTailT >= 80) {
            adoptTailT = now;
            adoptTailFrame = (adoptTailFrame + 1) % TAIL_NEUTRAL_FRAMES_COUNT;
        }
        // Blink eyes every 200 ms
        if (now - adoptEyeT >= 200) {
            adoptEyeT = now;
            adoptEyeFrame = (adoptEyeFrame + 1) % EYES_SIDE_NEUTRAL_FRAMES_COUNT;
        }
        // Body (sitting, 1 frame)
        display.drawBitmap(catX, 37, BODY_SIDE_SITTING_FRAMES[0], 21, 19, SSD1306_WHITE);
        // Animated tail
        display.drawBitmap(catX + 8, 36, TAIL_NEUTRAL_FRAMES[adoptTailFrame], 14, 21, SSD1306_WHITE);
        // Head
        display.drawBitmap(catX - 4, 23, HEAD_SIDE_NEUTRAL_FRAMES[0], 17, 24, SSD1306_WHITE);
        // Animated eyes overlaid on head
        display.drawBitmap(catX - 4 + 3, 23 + 10, EYES_SIDE_NEUTRAL_FRAMES[adoptEyeFrame], 14, 3, SSD1306_WHITE);
        // Exclamation bubble
        if (adoptBubbleProg > 0.2f)
            display.drawBitmap(catX + 10, 20, EXCLAIM, EXCLAIM_W, EXCLAIM_H, SSD1306_WHITE);
        // Heart bubble
        if (adoptBubbleProg > 0.6f)
            display.drawBitmap(catX + 22, 18, icon_heart, ICON_WIDTH, ICON_HEIGHT, SSD1306_WHITE);
    }
    display.setCursor(20, 60);
    display.print(F("Your new friend!"));
    display.display();
}

void adoptComplete() {
    const AdoptCandidate& c = adoptCandidates[adoptViewing];
    catPet.hunger = c.hunger;
    catPet.energy = c.energy;
    catPet.health = c.health;
    catPet.mood = c.mood;
    catPet.cleanliness = c.cleanliness;
    catPet.affection = c.affection;
    catPet.fitness = c.fitness;
    catPet.serenity = c.serenity;
    catPet.courage = c.courage;
    catPet.loyalty = c.loyalty;
    catPet.mischievousness = c.mischievousness;
    catPet.curiosity = c.curiosity;
    catPet.sociability = c.sociability;
    catPet.intelligence = c.intelligence;
    catPet.maturity = c.maturity;
    catPet.fulfillment = c.fulfillment;
    catPet.playfulness = c.playfulness;
    catPet.focus = c.focus;
    saveCatPet();
    prefs.begin("catpet", false);
    prefs.putBool("adopted", true);
    prefs.end();
}

void runAdoptionScene() {
    adoptGenerateCandidates();
    adoptState = ADOPT_GRID;
    adoptSelected = 0;
    clearBtnFlags();
    while (true) {
        Act a = getAction();
        switch (adoptState) {
            case ADOPT_GRID:
                if (a == A_UP || a == A_DOWN) {
                    uint8_t row = adoptSelected / 2;
                    row = 1 - row;
                    adoptSelected = row * 2 + (adoptSelected % 2);
                } else if (a == A_LEFT || a == A_RIGHT) {
                    uint8_t col = adoptSelected % 2;
                    col = 1 - col;
                    adoptSelected = (adoptSelected / 2) * 2 + col;
                } else if (a == A_RIGHT && (adoptSelected >= 4)) {
                    adoptSelected = 0;
                } else if (a == A_ALT_RIGHT) {
                    adoptViewing = adoptSelected;
                    adoptState = ADOPT_PROFILE;
                } else if (a == A_BACK) {
                    clearBtnFlags();
                    return;
                }
                adoptDrawGrid();
                break;
            case ADOPT_PROFILE:
                if (a == A_BACK) {
                    adoptState = ADOPT_GRID;
                } else if (a == A_ALT_RIGHT) {
                    adoptState = ADOPT_CONFIRM;
                }
                adoptDrawProfile();
                break;
            case ADOPT_CONFIRM:
                if (a == A_BACK) {
                    adoptState = ADOPT_PROFILE;
                } else if (a == A_ALT_RIGHT) {
                    adoptViewing = adoptSelected;
                    adoptMomentX = -20.0f;
                    adoptMomentTimer = 0.0f;
                    adoptBubbleProg = 0.0f;
                    adoptMomentPhase = ADOPT_WALKING;
                    adoptState = ADOPT_MOMENT;
                }
                adoptDrawConfirm();
                break;
            case ADOPT_MOMENT:
                if (adoptMomentPhase == ADOPT_WALKING) {
                    adoptMomentX += 1.0f;
                    if (adoptMomentX >= 64.0f) {
                        adoptMomentX = 64.0f;
                        adoptMomentPhase = ADOPT_SITTING;
                        adoptMomentTimer = 0.0f;
                    }
                } else {
                    adoptMomentTimer += 0.033f;
                    adoptBubbleProg = constrain(adoptMomentTimer / 3.0f, 0.0f, 1.0f);
                    if (adoptMomentTimer >= 4.0f) {
                        adoptComplete();
                        clearBtnFlags();
                        return;
                    }
                }
                adoptDrawMoment();
                delay(30);
                break;
        }
        delay(20);
    }
}