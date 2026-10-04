// ============================================================
//  FORECAST SCENE — Hourly weather timeline
//  Ported from Rust: forecast_scene.rs
// ============================================================

#include "neon_scene_api.h"
#include "generated_assets.h"
#include <cstdio>

struct ForecastSlot {
    uint8_t hour;
    uint8_t weather; // 0=Clear, 1=Cloudy, 2=Rain, 3=Snow
    int8_t temp_c;
};

static ForecastSlot forecastSlots[8];
static uint8_t forecastCursor = 0;

static const char* forecastWeatherName(uint8_t w) {
    switch (w) {
        case 0: return "Clear";
        case 1: return "Cloudy";
        case 2: return "Rain";
        case 3: return "Snow";
        default: return "?";
    }
}

// Dummy sun icon 18x18
static const uint8_t icon_sun[] PROGMEM = {
    0xff, 0xff, 0x80, 0x80, 0x00, 0x80, 0x80, 0x00, 0x80, 0x81, 0x08, 0x80, 0x82, 0x24, 0x80, 0x84, 0x12, 0x80, 0x88, 0x89, 0x80, 0x91, 0x44, 0x80, 0xa2, 0x25, 0x80, 0x91, 0x44, 0x80, 0x88, 0x89, 0x80, 0x84, 0x12, 0x80, 0x82, 0x24, 0x80, 0x81, 0x08, 0x80, 0x80, 0x00, 0x80, 0x80, 0x00, 0x80, 0xff, 0xff, 0x80, 0x80, 0x0f, 0x80, 0xff, 0xff, 0x80
};

static const uint8_t* forecastWeatherIcon(uint8_t w) {
    switch (w) {
        case 0: return icon_sun;
        case 1: return icon_sun;
        case 2: return icon_clean;
        case 3: return icon_star;
        default: return icon_question;
    }
}

static void forecastBuildSlots() {
    for (uint8_t i = 0; i < 8; i++) {
        forecastSlots[i].hour = (catPet.weather * 3 + i * 3) % 24;
        forecastSlots[i].weather = (catPet.weather + i / 3) % 4;
        forecastSlots[i].temp_c = 15 + (int8_t)(i * 2) - 5;
    }
}

static void forecastDraw() {
    clearDisp();
    // Header
    display.setCursor(2, 1);
    display.print(F("FORECAST"));
    display.drawFastHLine(0, 10, 128, SSD1306_WHITE);
    // Columns
    const int COL_W = 16;
    for (uint8_t i = 0; i < 8; i++) {
        int x = i * COL_W;
        // Hour
        char hourBuf[5];
        int h = forecastSlots[i].hour;
        if (h == 0) snprintf(hourBuf, sizeof(hourBuf), "12A");
        else if (h < 12) snprintf(hourBuf, sizeof(hourBuf), "%dA", h);
        else if (h == 12) snprintf(hourBuf, sizeof(hourBuf), "12P");
        else snprintf(hourBuf, sizeof(hourBuf), "%dP", h - 12);
        display.setCursor(x + 1, 13);
        display.print(hourBuf);
        // Icon
        drawCatIcon(forecastWeatherIcon(forecastSlots[i].weather), x, 26);
        // Temp
        display.setCursor(x + 1, 48);
        display.print(forecastSlots[i].temp_c);
    }
    // Highlight cursor
    display.drawRect(forecastCursor * COL_W, 11, COL_W, 40, SSD1306_WHITE);
    // Bottom info
    display.setCursor(2, 57);
    display.print(F("L/R:Scroll A:Back"));
    display.display();
}

void runForecastScene() {
    forecastBuildSlots();
    forecastCursor = 0;
    clearBtnFlags();
    while (true) {
        Act a = getAction();
        if (a == A_LEFT && forecastCursor > 0) forecastCursor--;
        if (a == A_RIGHT && forecastCursor < 7) forecastCursor++;
        if (a == A_BACK || a == A_ALT_RIGHT) {
            clearBtnFlags();
            return;
        }
        forecastDraw();
        delay(20);
    }
}