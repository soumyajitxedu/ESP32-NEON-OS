// =======@Madtektonik=======
// game.h - Multi-game library for SSD1306 OLED
// Buttons: 32=UP, 33=DOWN, 27=LEFT(long=BACK), 26=RIGHT

#ifndef GAME_H
#define GAME_H

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <SPI.h>
#include <Wire.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

// Button pins
#define BTN_UP     32
#define BTN_DOWN   33
#define BTN_LEFT   27   // long press = BACK
#define BTN_RIGHT  26
#define BTN_FIRE   14   // kept for shooter
#define BUZZER_PIN 4

// Long press threshold
#define LONG_PRESS_MS 600

// ===== Global display =====
extern Adafruit_SSD1306 display;

// ===== Button helper =====
inline bool btnPressed(uint8_t pin) {
  return digitalRead(pin) == LOW;
}

// Returns true once on short press release
inline bool btnShortPress(uint8_t pin) {
  static bool last[40] = {false};
  static unsigned long pressStart[40] = {0};
  static bool longFired[40] = {false};
  bool now = btnPressed(pin);
  bool result = false;
  if (pin < 40) {
    if (now && !last[pin]) {
      pressStart[pin] = millis();
      longFired[pin] = false;
    }
    if (!now && last[pin]) {
      if (!longFired[pin]) result = true;
    }
    if (now && !longFired[pin] && (millis() - pressStart[pin] > LONG_PRESS_MS)) {
      longFired[pin] = true;
    }
    last[pin] = now;
  }
  return result;
}

// Returns true once when long press threshold reached
inline bool btnLongPress(uint8_t pin) {
  static bool last[40] = {false};
  static unsigned long pressStart[40] = {0};
  static bool fired[40] = {false};
  bool now = btnPressed(pin);
  bool result = false;
  if (pin < 40) {
    if (now && !last[pin]) {
      pressStart[pin] = millis();
      fired[pin] = false;
    }
    if (!now) fired[pin] = false;
    if (now && !fired[pin] && (millis() - pressStart[pin] > LONG_PRESS_MS)) {
      fired[pin] = true;
      result = true;
    }
    last[pin] = now;
  }
  return result;
}

// ===== Shared sound helper =====
inline void beep(uint16_t freq, uint16_t dur) {
  tone(BUZZER_PIN, freq, dur);
}

// ============================================================
//                    SPACE SHOOTER GAME
// ============================================================
namespace Shooter {

const unsigned char spaceship_bmp[] PROGMEM = {
  0b00011000,
  0b00111100,
  0b01111110,
  0b11111111,
  0b01111110,
  0b00100100,
  0b01000010
};

// Star field for parallax background
#define STAR_COUNT 20
struct Star { int x, y, speed; };
Star stars[STAR_COUNT];

struct Bullet { int x, y; bool active; };
struct Enemy  { int x, y; int size; int hp; bool alive; };

const int maxBullets = 5;
Bullet bullets[maxBullets];
Enemy enemies[5];

int playerX, playerY;
int score, health;
bool gameOver;
unsigned long lastFireTime;
int enemySpeed;
int frameCount;

void initStars() {
  for (int i = 0; i < STAR_COUNT; i++) {
    stars[i].x = random(0, SCREEN_WIDTH);
    stars[i].y = random(0, SCREEN_HEIGHT);
    stars[i].speed = random(1, 3);
  }
}

void drawStars() {
  for (int i = 0; i < STAR_COUNT; i++) {
    display.drawPixel(stars[i].x, stars[i].y, SSD1306_WHITE);
  }
}

void updateStars() {
  for (int i = 0; i < STAR_COUNT; i++) {
    stars[i].y += stars[i].speed;
    if (stars[i].y >= SCREEN_HEIGHT) {
      stars[i].y = 0;
      stars[i].x = random(0, SCREEN_WIDTH);
    }
  }
}

void reset() {
  gameOver = false;
  score = 0;
  health = 10;
  enemySpeed = 1;
  frameCount = 0;
  playerX = SCREEN_WIDTH / 2;
  playerY = SCREEN_HEIGHT - 16;
  for (int i = 0; i < maxBullets; i++) bullets[i].active = false;
  for (int i = 0; i < 5; i++) {
    enemies[i] = { random(0, SCREEN_WIDTH - 10), random(-60, -10), random(6, 12), 1, true };
  }
  initStars();
}

void drawPlayer() {
  display.drawBitmap(playerX - 4, playerY, spaceship_bmp, 8, 7, SSD1306_WHITE);
  // Engine flame flicker
  if ((frameCount / 3) % 2 == 0) {
    display.drawLine(playerX, playerY + 7, playerX, playerY + 9, SSD1306_WHITE);
  } else {
    display.drawLine(playerX - 1, playerY + 7, playerX + 1, playerY + 9, SSD1306_WHITE);
  }
}

void drawBullets() {
  for (int i = 0; i < maxBullets; i++) {
    if (bullets[i].active) {
      display.drawFastVLine(bullets[i].x, bullets[i].y, 4, SSD1306_WHITE);
      display.drawPixel(bullets[i].x, bullets[i].y - 1, SSD1306_WHITE);
    }
  }
}

void drawEnemies() {
  for (int i = 0; i < 5; i++) {
    if (enemies[i].alive) {
      int x = enemies[i].x;
      int y = enemies[i].y;
      int s = enemies[i].size;
      int cx = x + s / 2;
      int cy = y + s / 2;
      display.drawCircle(cx, cy, s / 2, SSD1306_WHITE);
      display.drawLine(x, cy, x + s, cy, SSD1306_WHITE);
      display.drawLine(cx, y, cx, y + s, SSD1306_WHITE);
      // Eyes
      display.drawPixel(cx - 1, cy - 1, SSD1306_WHITE);
      display.drawPixel(cx + 1, cy - 1, SSD1306_WHITE);
    }
  }
}

void updateBullets() {
  for (int i = 0; i < maxBullets; i++) {
    if (bullets[i].active) {
      bullets[i].y -= 6;
      if (bullets[i].y < 0) bullets[i].active = false;
    }
  }
}

void updateEnemies() {
  for (int i = 0; i < 5; i++) {
    if (enemies[i].alive) {
      enemies[i].y += enemySpeed;
      if (enemies[i].y > SCREEN_HEIGHT) {
        health--;
        enemies[i].x = random(0, SCREEN_WIDTH - 10);
        enemies[i].y = random(-60, -10);
        enemies[i].size = random(6, 12);
        beep(300, 60);
      }
      if (enemies[i].y + enemies[i].size >= playerY &&
          enemies[i].x + enemies[i].size / 2 >= playerX - 4 &&
          enemies[i].x + enemies[i].size / 2 <= playerX + 4) {
        gameOver = true;
      }
    }
  }
}

void fire() {
  if (millis() - lastFireTime < 180) return;
  for (int i = 0; i < maxBullets; i++) {
    if (!bullets[i].active) {
      bullets[i].x = playerX;
      bullets[i].y = playerY;
      bullets[i].active = true;
      beep(1200, 20);
      lastFireTime = millis();
      break;
    }
  }
}

void checkCollisions() {
  for (int i = 0; i < maxBullets; i++) {
    if (bullets[i].active) {
      for (int j = 0; j < 5; j++) {
        int s = enemies[j].size;
        if (enemies[j].alive &&
            bullets[i].x > enemies[j].x && bullets[i].x < enemies[j].x + s &&
            bullets[i].y < enemies[j].y + s && bullets[i].y > enemies[j].y) {
          bullets[i].active = false;
          score += 5;
          // Explosion particles
          for (int k = 0; k < 4; k++) {
            display.drawPixel(enemies[j].x + random(0, s), enemies[j].y + random(0, s), SSD1306_WHITE);
          }
          enemies[j].x = random(0, SCREEN_WIDTH - 10);
          enemies[j].y = random(-60, -10);
          enemies[j].size = random(6, 12);
          beep(800, 20);
          if (score % 25 == 0 && enemySpeed < 5) enemySpeed++;
        }
      }
    }
  }
}

void run() {
  reset();
  unsigned long lastFrame = millis();
  while (true) {
    // BACK button
    if (btnLongPress(BTN_LEFT)) {
      beep(400, 80);
      return;
    }

    unsigned long now = millis();
    if (now - lastFrame < 30) { delay(2); continue; }
    lastFrame = now;
    frameCount++;

    // Input (LEFT/RIGHT for movement, UP to shoot)
    if (btnPressed(BTN_LEFT) && playerX > 6) playerX -= 3;
    if (btnPressed(BTN_RIGHT) && playerX < SCREEN_WIDTH - 6) playerX += 3;
    if (btnPressed(BTN_UP) || btnPressed(BTN_FIRE)) fire();

    updateStars();
    updateBullets();
    updateEnemies();
    checkCollisions();

    if (gameOver || health <= 0) {
      beep(200, 300);
      display.clearDisplay();
      display.setTextSize(2);
      display.setCursor(12, 15);
      display.print("GAME OVER");
      display.setTextSize(1);
      display.setCursor(35, 40);
      display.print("Score: ");
      display.print(score);
      display.display();
      delay(2000);
      while (btnPressed(BTN_UP) || btnPressed(BTN_DOWN) ||
             btnPressed(BTN_LEFT) || btnPressed(BTN_RIGHT)) delay(10);
      reset();
      continue;
    }

    display.clearDisplay();
    drawStars();
    drawBullets();
    drawEnemies();
    drawPlayer();

    // HUD
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print("S:"); display.print(score);
    display.setCursor(90, 0);
    display.print("HP:");
    // Health bar
    display.drawRect(108, 0, 20, 7, SSD1306_WHITE);
    display.fillRect(109, 1, (health * 18) / 10, 5, SSD1306_WHITE);

    display.display();
  }
}

} // namespace Shooter

// ============================================================
//                        PONG GAME
// ============================================================
namespace Pong {

const unsigned long PADDLE_RATE = 33;
const unsigned long BALL_RATE = 16;
const uint8_t PADDLE_HEIGHT = 20;
const uint8_t PADDLE_WIDTH = 3;

const uint8_t PLAYER_X = 120;
const uint8_t CPU_X = 5;

int8_t ball_x, ball_y;
int8_t ball_dir_x, ball_dir_y;
unsigned long ball_update;
unsigned long paddle_update;
int8_t cpu_y;
int8_t player_y;
int playerScore, cpuScore;

void drawCourt() {
  // Dotted center line
  for (int y = 2; y < SCREEN_HEIGHT - 2; y += 4) {
    display.drawPixel(SCREEN_WIDTH / 2, y, SSD1306_WHITE);
  }
  display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);
}

void resetBall(int dir) {
  ball_x = SCREEN_WIDTH / 2;
  ball_y = SCREEN_HEIGHT / 2;
  ball_dir_x = dir;
  ball_dir_y = (random(0, 2) == 0) ? 1 : -1;
}

void reset() {
  cpu_y = (SCREEN_HEIGHT - PADDLE_HEIGHT) / 2;
  player_y = (SCREEN_HEIGHT - PADDLE_HEIGHT) / 2;
  playerScore = 0;
  cpuScore = 0;
  resetBall(1);
  ball_update = millis();
  paddle_update = millis();
}

void drawPaddle(uint8_t x, int8_t y) {
  display.fillRect(x, y, PADDLE_WIDTH, PADDLE_HEIGHT, SSD1306_WHITE);
}

void showScore() {
  display.setTextSize(1);
  display.setCursor(SCREEN_WIDTH / 2 - 22, 2);
  display.print(cpuScore);
  display.setCursor(SCREEN_WIDTH / 2 + 14, 2);
  display.print(playerScore);
}

void run() {
  reset();
  display.clearDisplay();
  drawCourt();
  showScore();
  display.display();
  delay(800);

  while (true) {
    // BACK button
    if (btnLongPress(BTN_LEFT)) {
      beep(400, 80);
      return;
    }

    bool update = false;
    unsigned long now = millis();

    // ---- Ball ----
    if (now > ball_update) {
      ball_update += BALL_RATE;

      int8_t new_x = ball_x + ball_dir_x;
      int8_t new_y = ball_y + ball_dir_y;

      // Top/bottom walls
      if (new_y <= 1 || new_y >= SCREEN_HEIGHT - 2) {
        ball_dir_y = -ball_dir_y;
        new_y += ball_dir_y + ball_dir_y;
        beep(600, 15);
      }

      // Score (left/right)
      if (new_x <= 1) {
        playerScore++;
        beep(1000, 80);
        resetBall(1);
        new_x = ball_x; new_y = ball_y;
      }
      if (new_x >= SCREEN_WIDTH - 2) {
        cpuScore++;
        beep(500, 80);
        resetBall(-1);
        new_x = ball_x; new_y = ball_y;
      }

      // CPU paddle
      if (new_x <= CPU_X + PADDLE_WIDTH && new_x >= CPU_X &&
          new_y >= cpu_y && new_y <= cpu_y + PADDLE_HEIGHT) {
        ball_dir_x = -ball_dir_x;
        new_x += ball_dir_x + ball_dir_x;
        beep(800, 15);
      }

      // Player paddle
      if (new_x >= PLAYER_X && new_x <= PLAYER_X + PADDLE_WIDTH &&
          new_y >= player_y && new_y <= player_y + PADDLE_HEIGHT) {
        ball_dir_x = -ball_dir_x;
        new_x += ball_dir_x + ball_dir_x;
        beep(900, 15);
      }

      // Clear old ball
      display.drawPixel(ball_x, ball_y, BLACK);
      if (ball_x + 1 < SCREEN_WIDTH) display.drawPixel(ball_x + 1, ball_y, BLACK);

      ball_x = new_x;
      ball_y = new_y;

      // Draw ball (2px wide for visibility)
      display.drawPixel(ball_x, ball_y, SSD1306_WHITE);
      if (ball_x + 1 < SCREEN_WIDTH) display.drawPixel(ball_x + 1, ball_y, SSD1306_WHITE);

      update = true;
    }

    // ---- Paddles ----
    if (now > paddle_update) {
      paddle_update += PADDLE_RATE;

      // Erase CPU paddle
      display.fillRect(CPU_X, 1, PADDLE_WIDTH, SCREEN_HEIGHT - 2, BLACK);

      // CPU AI
      int cpuCenter = cpu_y + PADDLE_HEIGHT / 2;
      if (cpuCenter < ball_y - 2) cpu_y += 1;
      if (cpuCenter > ball_y + 2) cpu_y -= 1;
      if (cpu_y < 1) cpu_y = 1;
      if (cpu_y + PADDLE_HEIGHT > SCREEN_HEIGHT - 1)
        cpu_y = SCREEN_HEIGHT - 1 - PADDLE_HEIGHT;
      drawPaddle(CPU_X, cpu_y);

      // Erase player paddle
      display.fillRect(PLAYER_X, 1, PADDLE_WIDTH, SCREEN_HEIGHT - 2, BLACK);

      // Player input
      if (btnPressed(BTN_UP))   player_y -= 2;
      if (btnPressed(BTN_DOWN)) player_y += 2;
      if (player_y < 1) player_y = 1;
      if (player_y + PADDLE_HEIGHT > SCREEN_HEIGHT - 1)
        player_y = SCREEN_HEIGHT - 1 - PADDLE_HEIGHT;
      drawPaddle(PLAYER_X, player_y);

      update = true;
    }

    if (update) {
      // Redraw court (top/bottom/sides) lightly
      // Keep score overlay
      showScore();

      // Win condition
      if (playerScore >= 7 || cpuScore >= 7) {
        display.clearDisplay();
        display.setTextSize(2);
        display.setCursor(10, 15);
        display.print(playerScore >= 7 ? "YOU WIN!" : "CPU WINS");
        display.setTextSize(1);
        display.setCursor(35, 45);
        display.print(playerScore);
        display.print(" - ");
        display.print(cpuScore);
        display.display();
        beep(playerScore >= 7 ? 1500 : 300, 400);
        delay(2500);
        while (btnPressed(BTN_UP) || btnPressed(BTN_DOWN) ||
               btnPressed(BTN_LEFT) || btnPressed(BTN_RIGHT)) delay(10);
        reset();
        display.clearDisplay();
        drawCourt();
        showScore();
      }

      display.display();
    }

    delay(1);
  }
}

} // namespace Pong

// ============================================================
//                     MAIN MENU SYSTEM
// ============================================================
namespace GameMenu {

int selected = 0;
const int itemCount = 2;
const char* items[] = { "SPACE SHOOTER", "PONG" };

// Animated title decoration
int titleFrame = 0;

void drawStars() {
  static int sx[12], sy[12], ss[12];
  static bool init = false;
  if (!init) {
    for (int i = 0; i < 12; i++) {
      sx[i] = random(0, SCREEN_WIDTH);
      sy[i] = random(0, SCREEN_HEIGHT);
      ss[i] = random(1, 3);
    }
    init = true;
  }
  for (int i = 0; i < 12; i++) {
    display.drawPixel(sx[i], sy[i], SSD1306_WHITE);
    sy[i] += ss[i];
    if (sy[i] >= SCREEN_HEIGHT) {
      sy[i] = 0;
      sx[i] = random(0, SCREEN_WIDTH);
    }
  }
}

void draw() {
  display.clearDisplay();
  drawStars();

  // Title banner
  display.fillRect(0, 0, SCREEN_WIDTH, 14, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setTextSize(1);
  display.setCursor(28, 3);
  display.print("ARCADE OLED");
  display.setTextColor(SSD1306_WHITE);

  // Menu items
  for (int i = 0; i < itemCount; i++) {
    int y = 22 + i * 16;
    if (i == selected) {
      display.fillRect(6, y - 2, SCREEN_WIDTH - 12, 14, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
      // Arrow
      display.setCursor(10, y + 1);
      display.print(">");
      display.setCursor(22, y + 1);
      display.print(items[i]);
      display.setTextColor(SSD1306_WHITE);
    } else {
      display.setCursor(22, y + 1);
      display.print(items[i]);
    }
  }

  // Footer hint
  display.setTextSize(1);
  display.setCursor(4, 56);
  display.print("UP/DOWN  HOLD-LEFT=BACK");

  display.display();
}

void run() {
  selected = 0;
  unsigned long lastNav = 0;
  while (true) {
    if (millis() - lastNav > 180) {
      if (btnPressed(BTN_UP)) {
        selected--;
        if (selected < 0) selected = itemCount - 1;
        beep(700, 10);
        lastNav = millis();
      }
      if (btnPressed(BTN_DOWN)) {
        selected++;
        if (selected >= itemCount) selected = 0;
        beep(700, 10);
        lastNav = millis();
      }
    }

    if (btnShortPress(BTN_RIGHT) || btnPressed(BTN_FIRE)) {
      beep(1000, 40);
      delay(100);
      if (selected == 0) Shooter::run();
      else if (selected == 1) Pong::run();
    }

    // BACK exits menu (returns to menu loop)
    if (btnLongPress(BTN_LEFT)) {
      beep(400, 60);
      return;
    }

    draw();
    delay(30);
  }
}

} // namespace GameMenu

// ============================================================
//                      GLOBAL INIT
// ============================================================
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

inline void gamesSetup() {
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_LEFT, INPUT_PULLUP);
  pinMode(BTN_RIGHT, INPUT_PULLUP);
  pinMode(BTN_FIRE, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    // If this fails, hang (nothing we can do without display)
    for (;;);
  }
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.display();

  randomSeed(analogRead(A0));
}

inline void gamesLoop() {
  GameMenu::run();
}

#endif // GAME_H