#ifndef NEON_SCENE_API_H
#define NEON_SCENE_API_H

#include <Arduino.h>
#include <Adafruit_SSD1306.h>
#include <Preferences.h>
#include "cat_pet_types.h"
#include "cat_status_icons.h"

enum Act
{
    A_NONE,
    A_UP,
    A_DOWN,
    A_LEFT,
    A_RIGHT,
    A_BACK,
    A_ALT,
    A_ALT_DOWN,
    A_ALT_RIGHT
};

struct CatPetData
{
    uint8_t hunger = 80;
    uint8_t energy = 90;
    uint8_t health = 100;
    uint8_t mood = 80;
    uint8_t cleanliness = 80;
    uint8_t affection = 50;
    uint8_t fitness = 70;
    uint8_t serenity = 60;
    uint8_t courage = 45;
    uint8_t loyalty = 55;
    uint8_t mischievousness = 35;
    uint8_t curiosity = 70;
    uint8_t sociability = 45;
    uint8_t intelligence = 40;
    uint8_t maturity = 20;
    uint8_t fulfillment = 50;
    uint8_t playfulness = 65;
    uint8_t focus = 50;
    uint8_t coins = 20;
    uint8_t food = 3;
    uint8_t seeds = 1;
    uint8_t water = 3;
    uint8_t weather = 0;
    SeedKind selectedSeed = SEED_SUNFLOWER;
    PotKind selectedPot = POT_MEDIUM;
    Plant garden[3] = {};
};

extern Adafruit_SSD1306 display;
extern Preferences prefs;
extern CatPetData catPet;
extern bool catPetLoaded;

Act getAction();
void clearBtnFlags();
void clearDisp();
void drawHint(const char *msg);
void drawCatIcon(const uint8_t *icon, int x, int y);
void drawCatAvatar(int x, int y, uint8_t animationFrame);
uint8_t catClamp(int value);
const char *catWeatherShortName(uint8_t weather);
void saveCatPet();

void runAdoptionScene();
void runForecastScene();
void runTreehouseScene();
void runOutsideScene();

#endif
