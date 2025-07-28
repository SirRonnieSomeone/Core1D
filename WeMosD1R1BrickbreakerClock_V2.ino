#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <ESP8266WiFi.h>
#include <time.h>
#include "consts.h"

// Pin definitions for WeMos D1 R1
#define TFT_CS D2   // GPIO4
#define TFT_RST D3  // GPIO0
#define TFT_DC D4   // GPIO2
#define TFT_MOSI D7 // GPIO13
#define TFT_SCLK D5 // GPIO14
#define TFT_LED D1  // GPIO5

// WiFi credentials - replace with your network
const char* ssid = "BphPro";
const char* password = "bph12345";

// Display setup
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);

// Game constants optimized for ESP8266
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 128
#define PADDLE_WIDTH 20
#define PADDLE_HEIGHT 2
#define BALL_SIZE 3
#define BRICK_WIDTH 15
#define BRICK_HEIGHT 8
#define BRICK_ROWS 6
#define BRICK_COLS 8
#define MAX_PARTICLES 8
#define MAX_TRAILS 4
#define POWER_UP_BOUNCES 10
#define GAME_AREA_START 20
#define GAME_AREA_END 100
#define MAX_DIRTY_AREAS 20

// Title screen constants
#define TITLE_PARTICLES 6
#define TITLE_DISPLAY_TIME 4000

// Fixed-point constants (16.16 format for better precision)
#define FIXED_SHIFT 16
#define FIXED_ONE (1 << FIXED_SHIFT)
#define TO_FIXED(x) ((x) << FIXED_SHIFT)
#define FROM_FIXED(x) ((x) >> FIXED_SHIFT)

// Ball physics constants (all in fixed-point) - improved for consistent speed
#define BALL_SPEED_BASE TO_FIXED(5)     // Base speed: 5 pixels per frame (75 pps at 15fps)
#define BALL_SPEED_MAX TO_FIXED(7)      // Maximum speed: 7 pixels per frame (105 pps at 15fps)
#define BALL_SPEED_MIN TO_FIXED(4)      // Minimum speed: 4 pixels per frame (60 pps at 15fps)
#define MIN_ANGLE_VY TO_FIXED(2)        // Minimum vertical velocity to prevent shallow angles
#define PADDLE_COLLISION_LOOKAHEAD 4    // Pixels to check ahead for paddle collision

// Performance optimization constants
#define BALL_SIZE_2 6
#define PADDLE_WIDTH_HALF 10
#define SCREEN_WIDTH_MINUS_BALL 125
#define SCREEN_HEIGHT_MINUS_BALL 125
#define SCREEN_WIDTH_MINUS_PADDLE 108
#define GAME_AREA_START_PLUS_BALL 23
#define BRICK_WIDTH_MINUS_1 14
#define BRICK_HEIGHT_MINUS_1 7
#define BRICK_WIDTH_HALF 7
#define BRICK_HEIGHT_HALF 4

// Colors
#define COLOR_BLUE 0x001F
#define COLOR_GREEN 0x07E0
#define COLOR_YELLOW 0xFFE0
#define COLOR_RED 0xF800
#define COLOR_WHITE 0xFFFF
#define COLOR_BLACK 0x0000
#define COLOR_ORANGE 0xFD20
#define COLOR_GRAY 0x7BEF
#define COLOR_CYAN 0x07FF
#define COLOR_MAGENTA 0xF81F

// Game states
enum GameState {
  STATE_TITLE,
  STATE_PLAYING
};

// Optimized game structures with consistent fixed-point
struct Ball {
  int32_t x, y;           // Fixed-point position (16.16 format)
  int32_t vx, vy;         // Fixed-point velocity (16.16 format)
  int16_t oldX, oldY;     // Integer screen positions for rendering
  uint8_t powerMode : 1;
  uint8_t moving : 1;
  uint8_t dirty : 2;
  uint8_t powerBounces;
  uint32_t currentSpeed;  // Current speed in fixed-point for consistency
};

struct Paddle {
  int32_t x, y;           // Fixed-point position (16.16 format)
  int32_t targetX;        // Fixed-point target position
  int16_t oldX;           // Integer screen position for rendering
  uint8_t dirty : 1;
};

struct Brick {
  uint8_t x, y;
  uint8_t type : 2;
  uint8_t active : 1;
  uint8_t dirty : 1;
};

struct Particle {
  int16_t x, y;
  int16_t oldX, oldY;
  int8_t vx, vy;
  uint8_t life;
  uint16_t color;
  uint8_t active : 1;
  uint8_t dirty : 1;
  uint8_t fadeIndex : 3;
};

struct Trail {
  int16_t x, y;
  int16_t oldX, oldY;
  uint8_t life;
  uint8_t active : 1;
  uint8_t dirty : 1;
};

struct TitleParticle {
  int16_t x, y;
  int8_t vx, vy;
  uint16_t color;
  uint8_t life;
  uint8_t maxLife;
  uint8_t active : 1;
  uint8_t fadeIndex : 3;
};

struct BrickPosition {
  uint8_t x, y;
  uint8_t left, right, top, bottom;
};

struct DirtyRect {
  uint8_t x, y, w, h;
  uint8_t dirty : 1;
};

// Game variables
GameState gameState = STATE_TITLE;
Ball ball;
Paddle paddle;
Brick bricks[BRICK_ROWS][BRICK_COLS];
Particle particles[MAX_PARTICLES];
Trail trails[MAX_TRAILS];
TitleParticle titleParticles[TITLE_PARTICLES];
BrickPosition brickPositions[BRICK_ROWS][BRICK_COLS];
DirtyRect dirtyAreas[MAX_DIRTY_AREAS];
uint8_t dirtyCount = 0;
uint16_t score = 0;
int16_t oldScore = -1;
uint8_t level = 1;
int8_t oldLevel = -1;
uint8_t gameRunning = 1;
uint8_t gameInitialized = 0;
uint32_t gameStartDelay = 0;
uint32_t titleStartTime = 0;
uint32_t lastUpdate = 0;
const uint8_t targetFPS = 15;
const uint16_t frameTime = 1000 / targetFPS;  // ~67ms per frame
uint32_t accumulatedTime = 0;

// Time variables
struct tm timeinfo;
uint8_t timeValid = 0;
uint32_t lastTimeUpdate = 0;
int8_t lastMinute = -1;
char timeStr[16] = "";
char oldTimeStr[16] = "";
char dateStr[16] = "";
char oldDateStr[16] = "";
uint8_t timeDisplayDirty = 1;

// Pre-allocated color lookups
const uint16_t brickColors[4] PROGMEM = {COLOR_BLACK, COLOR_BLUE, COLOR_GREEN, COLOR_YELLOW};
const uint16_t titleColors[6] PROGMEM = {COLOR_RED, COLOR_GREEN, COLOR_BLUE, COLOR_YELLOW, COLOR_CYAN, COLOR_MAGENTA};

// Performance counters
uint16_t frameCounter = 0;
uint8_t particleUpdateCounter = 0;
uint8_t trailCounter = 0;

// Pre-calculated values - adjusted for 15 FPS
const int32_t paddleAISpeed = TO_FIXED(6);    // 6 pixels per frame max speed
const int16_t gravityConstant = 26;           // Particle gravity
const uint8_t particleLifeMin = 10;
const uint8_t particleLifeMax = 20;
const uint8_t trailLife = 8;
const uint8_t titleParticleLifeMin = 60;
const uint8_t titleParticleLifeMax = 120;

// Frame skip patterns
uint8_t frameSkipIndex = 0;
uint8_t currentFrameSkipPattern = 0;

// Simple and reliable velocity management - no complex normalization
void setVelocity(int32_t& vx, int32_t& vy, int32_t targetSpeed) {
  // Simple approach: maintain speed components within reasonable bounds
  // This prevents the inconsistent speed issues from complex normalization
  
  // Constrain individual components to reasonable ranges
  vx = constrain(vx, -targetSpeed, targetSpeed);
  vy = constrain(vy, -targetSpeed, targetSpeed);
  
  // Ensure minimum movement to prevent stalling
  if (abs(vx) < TO_FIXED(2)) {
    vx = (vx >= 0) ? TO_FIXED(2) : -TO_FIXED(2);
  }
  if (abs(vy) < TO_FIXED(2)) {
    vy = (vy >= 0) ? TO_FIXED(2) : -TO_FIXED(2);
  }
  
  // Store current speed for consistency tracking
  ball.currentSpeed = targetSpeed;
}

void setup() {
  Serial.begin(115200);
  Serial.println(F("Starting Polished HyperClock Brick..."));
  
  pinMode(TFT_LED, OUTPUT);
  digitalWrite(TFT_LED, HIGH);
  
  tft.initR(INITR_144GREENTAB);
  tft.setRotation(0);
  tft.fillScreen(COLOR_BLACK);
  
  preCalculateValues();
  connectToWiFi();
  configTime(5.5 * 3600, 0, "pool.ntp.org", "time.nist.gov");
  showTitleScreen();
  
  Serial.println(F("Optimization complete!"));
}

void loop() {
  uint32_t currentTime = millis();
  uint32_t deltaTime = currentTime - lastUpdate;
  lastUpdate = currentTime;
  accumulatedTime += deltaTime;

  // Update time less frequently - every 30 seconds at 15fps
  if (currentTime - lastTimeUpdate >= 30000) {
    updateTimeDisplay();
    lastTimeUpdate = currentTime;
  }

  // Fixed time step update
  while (accumulatedTime >= frameTime) {
    if (shouldSkipFrame()) {
      accumulatedTime -= frameTime;
      continue;
    }

    if (gameState == STATE_TITLE) {
      updateTitleScreen();
      renderTitleScreen();
      if (currentTime - titleStartTime >= TITLE_DISPLAY_TIME) {
        gameState = STATE_PLAYING;
        initGame();
        addDirtyRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
        Serial.println(F("Game started"));
      }
    } else {
      updateGame();
      renderDirtyAreas();
    }
    accumulatedTime -= frameTime;
    frameCounter++;
    
    if ((frameCounter & 0x3F) == 0) {
      adjustPerformance();
    }
  }

  // Yield to WiFi stack sparingly
  if ((frameCounter & 0x1F) == 0) {
    yield();
  }
}

bool shouldSkipFrame() {
  // No frame skipping needed at 15 FPS base
  return false;
}

void adjustPerformance() {
  // Performance is already optimized for 15 FPS
  // No dynamic adjustment needed
}

void preCalculateValues() {
  for (uint8_t row = 0; row < BRICK_ROWS; row++) {
    for (uint8_t col = 0; col < BRICK_COLS; col++) {
      BrickPosition& pos = brickPositions[row][col];
      pos.x = col * BRICK_WIDTH + 2;
      pos.y = row * BRICK_HEIGHT + GAME_AREA_START;
      pos.left = pos.x;
      pos.right = pos.x + BRICK_WIDTH;
      pos.top = pos.y;
      pos.bottom = pos.y + BRICK_HEIGHT;
    }
  }
}

void connectToWiFi() {
  tft.fillScreen(COLOR_BLACK);
  tft.setTextColor(COLOR_WHITE, COLOR_BLACK);
  tft.setTextSize(1);
  tft.setCursor(10, 50);
  tft.println(F("Connecting WiFi..."));
  
  WiFi.begin(ssid, password);
  
  uint8_t attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(F("WiFi connected"));
    tft.setCursor(10, 70);
    tft.println(F("WiFi Connected!"));
  } else {
    Serial.println(F("WiFi failed"));
    tft.setCursor(10, 70);
    tft.println(F("WiFi Failed!"));
  }
  delay(500);
}

void showTitleScreen() {
  titleStartTime = millis();
  gameState = STATE_TITLE;
  initTitleParticles();
  
  tft.fillScreen(COLOR_BLACK);
  tft.setTextColor(COLOR_WHITE, COLOR_BLACK);
  tft.setTextSize(1);
  
  tft.setCursor(8, 30);
  tft.println(F("Core1D Automation"));
  tft.setCursor(28, 42);
  tft.println(F("Labs present:"));
  
  tft.setTextColor(COLOR_CYAN, COLOR_BLACK);
  tft.setCursor(18, 65);
  tft.println(F("HyperClock"));
  tft.setCursor(35, 77);
  tft.println(F("Brick"));
}

void initTitleParticles() {
  for (uint8_t i = 0; i < TITLE_PARTICLES; i++) {
    TitleParticle& p = titleParticles[i];
    p.x = random(10, SCREEN_WIDTH - 10);
    p.y = random(10, SCREEN_HEIGHT - 10);
    uint16_t angle = random(0, 360);
    p.vx = cos(angle * 3.14159 / 180) * 2;
    p.vy = sin(angle * 3.14159 / 180) * 2;
    p.color = pgm_read_word(&titleColors[i % 6]);
    p.life = random(titleParticleLifeMin, titleParticleLifeMax);
    p.maxLife = p.life;
    p.active = 1;
    p.fadeIndex = 0;
  }
}

void updateTitleScreen() {
  updateTitleParticles();
}

void updateTitleParticles() {
  for (uint8_t i = 0; i < TITLE_PARTICLES; i++) {
    TitleParticle& p = titleParticles[i];
    
    if (!p.active) {
      p.x = random(10, SCREEN_WIDTH - 10);
      p.y = random(10, SCREEN_HEIGHT - 10);
      uint16_t angle = random(0, 360);
      p.vx = cos(angle * 3.14159 / 180) * 2;
      p.vy = sin(angle * 3.14159 / 180) * 2;
      p.color = pgm_read_word(&titleColors[i % 6]);
      p.life = random(titleParticleLifeMin, titleParticleLifeMax);
      p.maxLife = p.life;
      p.active = 1;
      p.fadeIndex = 0;
    }
    
    p.x += p.vx;
    p.y += p.vy;
    
    if (p.x <= 0 || p.x >= SCREEN_WIDTH) {
      p.vx = -p.vx;
      p.x = constrain(p.x, 0, SCREEN_WIDTH - 1);
    }
    
    if (p.y <= 0 || p.y >= SCREEN_HEIGHT) {
      p.vy = -p.vy;
      p.y = constrain(p.y, 0, SCREEN_HEIGHT - 1);
    }
    
    p.life--;
    p.fadeIndex = map(p.life, 0, p.maxLife, 4, 0);
    if (p.life <= 0) {
      p.active = 0;
    }
  }
}

void renderTitleScreen() {
  static uint8_t titleFrameCounter = 0;
  titleFrameCounter++;
  
  for (uint8_t i = 0; i < TITLE_PARTICLES; i++) {
    const TitleParticle& p = titleParticles[i];
    if (p.active) {
      uint16_t fadeColor = p.color;
      if (p.fadeIndex > 2) {
        fadeColor = COLOR_GRAY;
      }
      tft.drawPixel(p.x, p.y, fadeColor);
      
      if ((titleFrameCounter & 0x03) == 0) {
        tft.drawPixel(p.x - p.vx, p.y - p.vy, COLOR_BLACK);
      }
    }
  }
  
  if ((titleFrameCounter & 0x3F) == 0) {
    tft.setTextColor(COLOR_WHITE, COLOR_BLACK);
    tft.setTextSize(1);
    
    tft.setCursor(8, 30);
    tft.println(F("Core1D Automation"));
    tft.setCursor(28, 42);
    tft.println(F("Labs present:"));
    
    tft.setTextColor(COLOR_CYAN, COLOR_BLACK);
    tft.setCursor(18, 65);
    tft.println(F("HyperClock"));
    tft.setCursor(35, 77);
    tft.println(F("Brick"));
  }
}

void initGame() {
  // Initialize ball with consistent fixed-point velocity
  ball.x = TO_FIXED(SCREEN_WIDTH / 2);
  ball.y = TO_FIXED(SCREEN_HEIGHT - 40);
  ball.oldX = FROM_FIXED(ball.x);
  ball.oldY = FROM_FIXED(ball.y);
  
  // Set initial velocity with consistent speed
  ball.vx = TO_FIXED(random(-3, 4));
  if (ball.vx == 0) ball.vx = TO_FIXED(3);
  ball.vy = TO_FIXED(-4);
  
  // Use simple velocity management
  setVelocity(ball.vx, ball.vy, BALL_SPEED_BASE);
  
  ball.powerMode = 0;
  ball.powerBounces = 0;
  ball.dirty = 1;
  ball.moving = 0;
  ball.currentSpeed = BALL_SPEED_BASE;
  
  paddle.x = TO_FIXED((SCREEN_WIDTH / 2) - PADDLE_WIDTH_HALF);
  paddle.y = TO_FIXED(SCREEN_HEIGHT - 12);
  paddle.oldX = FROM_FIXED(paddle.x);
  paddle.targetX = paddle.x;
  paddle.dirty = 1;
  
  memset(particles, 0, sizeof(particles));
  memset(trails, 0, sizeof(trails));
  
  initBricks();
  updateTimeDisplay();
  
  gameInitialized = 0;
  gameStartDelay = millis() + 2000;
}

void initBricks() {
  uint8_t patternIndex = (level - 1) % 5;
  
  for (uint8_t row = 0; row < BRICK_ROWS; row++) {
    for (uint8_t col = 0; col < BRICK_COLS; col++) {
      Brick& brick = bricks[row][col];
      const BrickPosition& pos = brickPositions[row][col];
      
      brick.x = pos.x;
      brick.y = pos.y;
      brick.active = 1;
      brick.dirty = 1;
      
      uint8_t brickIndex = row * BRICK_COLS + col;
      if (brickIndex < 48) {
        brick.type = pgm_read_byte(&brick_patterns[patternIndex][brickIndex]);
      } else {
        brick.type = 1;
      }
    }
  }
}

void updateTimeDisplay() {
  if (!getLocalTime(&timeinfo)) {
    timeValid = 0;
    return;
  }
  
  timeValid = 1;
  
  if (timeinfo.tm_min != lastMinute) {
    strcpy(oldTimeStr, timeStr);
    strcpy(oldDateStr, dateStr);
    
    snprintf_P(timeStr, sizeof(timeStr), PSTR("%02d:%02d:%02d"), 
               timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    snprintf_P(dateStr, sizeof(dateStr), PSTR("%02d/%02d/%04d"), 
               timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);
    
    timeDisplayDirty = 1;
    lastMinute = timeinfo.tm_min;
  }
}

void addDirtyRect(uint8_t x, uint8_t y, uint8_t w, uint8_t h) {
  if (dirtyCount >= MAX_DIRTY_AREAS) return;
  
  x = constrain(x, 0, SCREEN_WIDTH - 1);
  y = constrain(y, 0, SCREEN_HEIGHT - 1);
  w = min(w, (uint8_t)(SCREEN_WIDTH - x));
  h = min(h, (uint8_t)(SCREEN_HEIGHT - y));
  
  if (w > 0 && h > 0) {
    for (uint8_t i = 0; i < dirtyCount; i++) {
      DirtyRect& existing = dirtyAreas[i];
      if (existing.dirty && 
          x < existing.x + existing.w && x + w > existing.x &&
          y < existing.y + existing.h && y + h > existing.y) {
        existing.x = min(x, existing.x);
        existing.y = min(y, existing.y);
        existing.w = max(x + w, existing.x + existing.w) - existing.x;
        existing.h = max(y + h, existing.y + existing.h) - existing.y;
        return;
      }
    }
    
    DirtyRect& rect = dirtyAreas[dirtyCount];
    rect.x = x;
    rect.y = y;
    rect.w = w;
    rect.h = h;
    rect.dirty = 1;
    dirtyCount++;
  }
}

void updateGame() {
  if (!gameRunning) return;
  
  if (!gameInitialized) {
    if (millis() >= gameStartDelay) {
      gameInitialized = 1;
      ball.moving = 1;
    }
    return;
  }
  
  dirtyCount = 0;
  
  // Store old positions for proper dirty rectangle handling
  ball.oldX = FROM_FIXED(ball.x);
  ball.oldY = FROM_FIXED(ball.y);
  paddle.oldX = FROM_FIXED(paddle.x);
  
  // ALWAYS clear the old ball position first - this fixes afterimage issues
  addDirtyRect(ball.oldX - BALL_SIZE - 1, ball.oldY - BALL_SIZE - 1, BALL_SIZE_2 + 2, BALL_SIZE_2 + 2);
  
  if (ball.moving) {
    // Predictive paddle collision detection to prevent overlap
    checkPaddleCollisionPredictive();
    
    // Direct fixed-point movement with consistent speed
    ball.x += ball.vx;
    ball.y += ball.vy;
  }
  
  if (ball.powerMode && ball.moving && ((++trailCounter & 0x01) == 0)) {  // Every 2nd frame at 15fps
    addTrail(FROM_FIXED(ball.x), FROM_FIXED(ball.y));
  }
  
  // Ball collision with walls - maintain consistent speed
  if (FROM_FIXED(ball.x) <= BALL_SIZE) {
    ball.vx = -ball.vx;
    ball.x = TO_FIXED(BALL_SIZE);
    setVelocity(ball.vx, ball.vy, ball.currentSpeed);
    if (ball.powerMode && --ball.powerBounces <= 0) {
      ball.powerMode = 0;
      ball.currentSpeed = BALL_SPEED_BASE;
    }
  } else if (FROM_FIXED(ball.x) >= SCREEN_WIDTH_MINUS_BALL) {
    ball.vx = -ball.vx;
    ball.x = TO_FIXED(SCREEN_WIDTH_MINUS_BALL);
    setVelocity(ball.vx, ball.vy, ball.currentSpeed);
    if (ball.powerMode && --ball.powerBounces <= 0) {
      ball.powerMode = 0;
      ball.currentSpeed = BALL_SPEED_BASE;
    }
  }
  
  if (FROM_FIXED(ball.y) <= GAME_AREA_START_PLUS_BALL) {
    ball.vy = -ball.vy;
    ball.y = TO_FIXED(GAME_AREA_START_PLUS_BALL);
    setVelocity(ball.vx, ball.vy, ball.currentSpeed);
    if (ball.powerMode && --ball.powerBounces <= 0) {
      ball.powerMode = 0;
      ball.currentSpeed = BALL_SPEED_BASE;
    }
  }
  
  checkBrickCollisions();
  updateAIPaddle();
  
  if ((++particleUpdateCounter & 0x03) == 0) {  // Update particles every 4th frame at 15fps
    updateParticles();
  }
  
  updateTrails();
  
  // Mark new ball position for rendering - single dirty rect
  addDirtyRect(FROM_FIXED(ball.x) - BALL_SIZE - 1, FROM_FIXED(ball.y) - BALL_SIZE - 1, BALL_SIZE_2 + 2, BALL_SIZE_2 + 2);
  
  // Handle paddle dirty rectangles
  if (abs(FROM_FIXED(paddle.x) - paddle.oldX) > 0) {
    int16_t minX = min((int16_t)(paddle.oldX), (int16_t)FROM_FIXED(paddle.x)) - 1;
    int16_t maxX = max((int16_t)(paddle.oldX), (int16_t)FROM_FIXED(paddle.x)) + PADDLE_WIDTH + 1;
    addDirtyRect(minX, FROM_FIXED(paddle.y), maxX - minX, PADDLE_HEIGHT + 2);
  }
  
  if (FROM_FIXED(ball.y) > SCREEN_HEIGHT) {
    resetLevel();
  }
  
  if (allBricksDestroyed()) {
    level++;
    initBricks();
    addDirtyRect(0, GAME_AREA_START, SCREEN_WIDTH, GAME_AREA_END - GAME_AREA_START);
  }
}

// Improved predictive paddle collision detection
void checkPaddleCollisionPredictive() {
  if (!ball.moving || ball.vy <= 0) return;
  
  // Check if ball is approaching paddle area
  int16_t ballBottom = FROM_FIXED(ball.y) + BALL_SIZE;
  int16_t paddleTop = FROM_FIXED(paddle.y);
  
  // Look ahead to prevent overlap
  int16_t nextBallBottom = ballBottom + FROM_FIXED(ball.vy);
  
  if (ballBottom <= paddleTop && nextBallBottom >= paddleTop - PADDLE_COLLISION_LOOKAHEAD) {
    int16_t ballLeft = FROM_FIXED(ball.x) - BALL_SIZE;
    int16_t ballRight = FROM_FIXED(ball.x) + BALL_SIZE;
    int16_t paddleLeft = FROM_FIXED(paddle.x);
    int16_t paddleRight = paddleLeft + PADDLE_WIDTH;
    
    // Check horizontal overlap
    if (ballRight >= paddleLeft && ballLeft <= paddleRight) {
      // Calculate collision physics with momentum consideration
      handlePaddleCollision();
    }
  }
}

void handlePaddleCollision() {
  // Position ball just above paddle to prevent overlap
  ball.y = paddle.y - TO_FIXED(BALL_SIZE + 1);
  
  // Calculate relative hit position for spin effect
  int16_t ballCenterX = FROM_FIXED(ball.x);
  int16_t paddleCenterX = FROM_FIXED(paddle.x) + PADDLE_WIDTH_HALF;
  int16_t relativeX = ballCenterX - paddleCenterX;
  
  // Calculate momentum-based velocity change
  int32_t impactForce = abs(ball.vy) / 4;  // Convert downward momentum to upward force
  
  // Always reflect upward with momentum boost
  ball.vy = -(abs(ball.vy) + impactForce);
  
  // Apply controlled spin based on paddle hit position with momentum
  int32_t spinFactor = TO_FIXED(relativeX) / PADDLE_WIDTH_HALF;
  ball.vx += (spinFactor * impactForce) / TO_FIXED(2);
  
  // Use simple velocity management instead of complex normalization
  setVelocity(ball.vx, ball.vy, ball.currentSpeed);
  
  if (ball.powerMode && --ball.powerBounces <= 0) {
    ball.powerMode = 0;
    ball.currentSpeed = BALL_SPEED_BASE;
  }
}

void updateAIPaddle() {
  if (ball.vy > 0) {
    // Improved prediction with wall bounce consideration
    int32_t timeToReachPaddle = (paddle.y - ball.y) / ball.vy;
    int32_t predictedX = ball.x + (ball.vx * timeToReachPaddle);
    
    // Handle wall bounces in prediction
    while (FROM_FIXED(predictedX) < 0 || FROM_FIXED(predictedX) > SCREEN_WIDTH) {
      if (FROM_FIXED(predictedX) < 0) {
        predictedX = -predictedX;
      } else {
        predictedX = TO_FIXED(SCREEN_WIDTH * 2) - predictedX;
      }
    }
    
    paddle.targetX = predictedX - TO_FIXED(PADDLE_WIDTH_HALF);
  } else {
    paddle.targetX = TO_FIXED((SCREEN_WIDTH / 2) - PADDLE_WIDTH_HALF);
  }
  
  // Smooth paddle movement
  int32_t diff = paddle.targetX - paddle.x;
  if (abs(diff) > paddleAISpeed) {
    paddle.x += (diff > 0) ? paddleAISpeed : -paddleAISpeed;
  } else {
    paddle.x = paddle.targetX;
  }
  
  paddle.x = constrain(paddle.x, 0, TO_FIXED(SCREEN_WIDTH_MINUS_PADDLE));
}

void checkBrickCollisions() {
  if (!ball.moving) return;
  
  int16_t ballLeft = FROM_FIXED(ball.x) - BALL_SIZE;
  int16_t ballRight = FROM_FIXED(ball.x) + BALL_SIZE;
  int16_t ballTop = FROM_FIXED(ball.y) - BALL_SIZE;
  int16_t ballBottom = FROM_FIXED(ball.y) + BALL_SIZE;
  
  for (uint8_t row = 0; row < BRICK_ROWS; row++) {
    for (uint8_t col = 0; col < BRICK_COLS; col++) {
      Brick& brick = bricks[row][col];
      if (!brick.active) continue;
      
      const BrickPosition& pos = brickPositions[row][col];
      
      if (ballRight >= pos.left && ballLeft <= pos.right &&
          ballBottom >= pos.top && ballTop <= pos.bottom) {
        
        if (brick.type == 2) {
          ball.powerMode = 1;
          ball.powerBounces = POWER_UP_BOUNCES;
          ball.currentSpeed = BALL_SPEED_MAX;
        }
        
        createBrickParticles(pos.x + BRICK_WIDTH_HALF, pos.y + BRICK_HEIGHT_HALF, brick.type);
        addDirtyRect(pos.x - 2, pos.y - 2, BRICK_WIDTH + 4, BRICK_HEIGHT + 4);
        
        brick.active = 0;
        score += 10 * brick.type;
        
        if (!ball.powerMode) {
          // Improved collision side determination for proper reflection
          int16_t ballCenterX = FROM_FIXED(ball.x);
          int16_t ballCenterY = FROM_FIXED(ball.y);
          int16_t brickCenterX = pos.x + BRICK_WIDTH_HALF;
          int16_t brickCenterY = pos.y + BRICK_HEIGHT_HALF;
          
          int16_t deltaX = ballCenterX - brickCenterX;
          int16_t deltaY = ballCenterY - brickCenterY;
          
          // Determine primary collision direction with better accuracy
          if (abs(deltaX) * BRICK_HEIGHT > abs(deltaY) * BRICK_WIDTH) {
            // Hit left or right side - flip X velocity
            ball.vx = -ball.vx;
            // Add slight random variation to prevent infinite loops
            ball.vy += TO_FIXED(random(-1, 2));
          } else {
            // Hit top or bottom - flip Y velocity  
            ball.vy = -ball.vy;
            // Add slight random variation
            ball.vx += TO_FIXED(random(-1, 2));
          }
          
          // Maintain consistent speed with simple velocity management after collision
          setVelocity(ball.vx, ball.vy, ball.currentSpeed);
        } else {
          ball.powerBounces--;
          if (ball.powerBounces <= 0) {
            ball.powerMode = 0;
            ball.currentSpeed = BALL_SPEED_BASE;
          }
        }
        
        return;
      }
    }
  }
}

void createBrickParticles(uint8_t x, uint8_t y, uint8_t brickType) {
  uint16_t color = pgm_read_word(&brickColors[brickType]);
  
  for (uint8_t i = 0; i < 4; i++) {
    int8_t idx = findFreeParticle();
    if (idx >= 0) {
      Particle& p = particles[idx];
      p.oldX = p.x = x + random(-2, 3);
      p.oldY = p.y = y + random(-2, 3);
      p.vx = random(-3, 4);
      p.vy = random(-4, -1);
      p.life = random(particleLifeMin, particleLifeMax);
      p.color = color;
      p.active = 1;
      p.dirty = 1;
      p.fadeIndex = 0;
    }
  }
}

int8_t findFreeParticle() {
  for (uint8_t i = 0; i < MAX_PARTICLES; i++) {
    if (!particles[i].active) return i;
  }
  return -1;
}

void updateParticles() {
  for (uint8_t i = 0; i < MAX_PARTICLES; i++) {
    Particle& p = particles[i];
    if (!p.active) continue;
    
    p.oldX = p.x;
    p.oldY = p.y;
    
    p.x += p.vx;
    p.y += p.vy;
    p.vy += 1; // Simple gravity
    p.life--;
    p.fadeIndex = map(p.life, 0, particleLifeMax, 4, 0);
    
    if (p.life <= 0 || p.y > SCREEN_HEIGHT) {
      addDirtyRect(p.oldX - 1, p.oldY - 1, 3, 3);
      p.active = 0;
      p.dirty = 0;
    } else {
      addDirtyRect(p.oldX - 1, p.oldY - 1, 3, 3);
      addDirtyRect(p.x - 1, p.y - 1, 3, 3);
    }
  }
}

void addTrail(int16_t x, int16_t y) {
  for (uint8_t i = 0; i < MAX_TRAILS; i++) {
    Trail& t = trails[i];
    if (!t.active) {
      t.oldX = t.x = x;
      t.oldY = t.y = y;
      t.life = trailLife;
      t.active = 1;
      t.dirty = 1;
      break;
    }
  }
}

void updateTrails() {
  for (uint8_t i = 0; i < MAX_TRAILS; i++) {
    Trail& t = trails[i];
    if (!t.active) continue;
    
    t.life--;
    if (t.life <= 0) {
      addDirtyRect(t.x - 1, t.y - 1, 3, 3);
      t.active = 0;
      t.dirty = 0;
    }
  }
}

bool allBricksDestroyed() {
  for (uint8_t row = 0; row < BRICK_ROWS; row++) {
    for (uint8_t col = 0; col < BRICK_COLS; col++) {
      if (bricks[row][col].active) return false;
    }
  }
  return true;
}

void resetLevel() {
  Serial.println(F("Level reset"));
  initGame();
  addDirtyRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
}

void renderDirtyAreas() {
  if (score != oldScore || level != oldLevel) {
    addDirtyRect(0, 0, SCREEN_WIDTH, 16);
    oldScore = score;
    oldLevel = level;
  }
  
  if (timeDisplayDirty) {
    addDirtyRect(0, GAME_AREA_END + 5, SCREEN_WIDTH, 25);
    timeDisplayDirty = 0;
  }
  
  for (uint8_t i = 0; i < dirtyCount; i++) {
    const DirtyRect& rect = dirtyAreas[i];
    if (rect.dirty) {
      renderArea(rect.x, rect.y, rect.w, rect.h);
      dirtyAreas[i].dirty = 0;
    }
  }
  
  dirtyCount = 0;
}

void renderArea(uint8_t x, uint8_t y, uint8_t w, uint8_t h) {
  tft.fillRect(x, y, w, h, COLOR_BLACK);
  
  // Render bricks
  for (uint8_t row = 0; row < BRICK_ROWS; row++) {
    for (uint8_t col = 0; col < BRICK_COLS; col++) {
      const Brick& brick = bricks[row][col];
      if (!brick.active) continue;
      
      const BrickPosition& pos = brickPositions[row][col];
      
      if (pos.x < x + w && pos.x + BRICK_WIDTH > x &&
          pos.y < y + h && pos.y + BRICK_HEIGHT > y) {
        uint16_t color = pgm_read_word(&brickColors[brick.type]);
        tft.fillRect(pos.x, pos.y, BRICK_WIDTH_MINUS_1, BRICK_HEIGHT_MINUS_1, color);
      }
    }
  }
  
  // Render paddle
  if (FROM_FIXED(paddle.x) < x + w && FROM_FIXED(paddle.x) + PADDLE_WIDTH > x &&
      FROM_FIXED(paddle.y) < y + h && FROM_FIXED(paddle.y) + PADDLE_HEIGHT > y) {
    tft.fillRect(FROM_FIXED(paddle.x), FROM_FIXED(paddle.y), PADDLE_WIDTH, PADDLE_HEIGHT, COLOR_WHITE);
  }
  
  // Render ball
  if (FROM_FIXED(ball.x) - BALL_SIZE < x + w && FROM_FIXED(ball.x) + BALL_SIZE > x &&
      FROM_FIXED(ball.y) - BALL_SIZE < y + h && FROM_FIXED(ball.y) + BALL_SIZE > y) {
    uint16_t ballColor = ball.powerMode ? COLOR_RED : COLOR_WHITE;
    tft.fillCircle(FROM_FIXED(ball.x), FROM_FIXED(ball.y), BALL_SIZE, ballColor);
  }
  
  // Render trails
  for (uint8_t i = 0; i < MAX_TRAILS; i++) {
    const Trail& t = trails[i];
    if (t.active && t.x >= x && t.x < x + w && t.y >= y && t.y < y + h) {
      uint8_t brightness = map(t.life, 0, trailLife, 64, 255);
      uint16_t trailColor = ((brightness >> 3) << 11) | ((brightness >> 2) << 5) | (brightness >> 3);
      tft.drawPixel(t.x, t.y, trailColor);
    }
  }
  
  // Render particles
  for (uint8_t i = 0; i < MAX_PARTICLES; i++) {
    const Particle& p = particles[i];
    if (p.active && p.x >= x && p.x < x + w && p.y >= y && p.y < y + h) {
      uint16_t fadeColor = p.color;
      if (p.fadeIndex > 2) {
        uint8_t fade = map(p.life, 0, particleLifeMax, 32, 255);
        fadeColor = ((fade >> 3) << 11) | ((fade >> 2) << 5) | (fade >> 3);
      }
      tft.drawPixel(p.x, p.y, fadeColor);
    }
  }
  
  // Render UI elements
  if (y < 16) {
    tft.setTextColor(COLOR_WHITE, COLOR_BLACK);
    tft.setTextSize(1);
    
    if (x < 60) {
      tft.setCursor(2, 2);
      tft.print(F("Score: "));
      tft.print(score);
    }
    
    if (x + w > 60) {
      tft.setCursor(2, 12);
      tft.print(F("Level: "));
      tft.print(level);
    }
    
    if (ball.powerMode && x + w > 80) {
      tft.setCursor(80, 2);
      tft.setTextColor(COLOR_RED, COLOR_BLACK);
      tft.print(F("POWER!"));
    }
  }
  
  // Render time display
  if (y < GAME_AREA_END + 30 && y + h > GAME_AREA_END + 5 && timeValid) {
    tft.setTextColor(COLOR_YELLOW, COLOR_BLACK);
    tft.setTextSize(1);
    
    uint8_t timeLen = strlen(timeStr);
    uint8_t dateLen = strlen(dateStr);
    uint8_t timeX = (SCREEN_WIDTH - (timeLen * 6)) >> 1;
    uint8_t dateX = (SCREEN_WIDTH - (dateLen * 6)) >> 1;
    
    tft.setCursor(timeX, GAME_AREA_END + 8);
    tft.print(timeStr);
    
    tft.setCursor(dateX, GAME_AREA_END + 18);
    tft.print(dateStr);
  }
}