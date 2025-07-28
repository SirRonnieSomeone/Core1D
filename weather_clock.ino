#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <TimeLib.h>

// Pin definitions for WeMos D1 R1
#define TFT_CS D2   // GPIO4
#define TFT_RST D3  // GPIO0
#define TFT_DC D4   // GPIO2
#define TFT_MOSI D7 // GPIO13
#define TFT_SCLK D5 // GPIO14

// Display initialization
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);

// WiFi credentials - Update these with your network details
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// Weather API configuration
String WEATHER_API_KEY = "cb840413161e7ee08e831af35dfb9c53";
String WEATHER_BASE_URL = "http://api.openweathermap.org/data/2.5/weather?id=";
String MUMBAI_CITY_ID = "1275339"; // Mumbai city ID

// NTP Client setup for IST (UTC+5:30)
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 19800, 60000); // IST offset: 19800 seconds

// Weather data structure
struct WeatherData {
  String condition;
  float temperature;
  int humidity;
  float windSpeed;
  String description;
  bool isValid;
};

WeatherData currentWeather;

// Physics particles for weather effects
struct Particle {
  float x, y;
  float vx, vy;
  float life;
  uint16_t color;
  bool active;
};

const int MAX_PARTICLES = 20;
Particle particles[MAX_PARTICLES];

// Seven segment digit patterns (for 0-9)
const uint8_t digitPatterns[10][7] = {
  {1,1,1,1,1,1,0}, // 0
  {0,1,1,0,0,0,0}, // 1
  {1,1,0,1,1,0,1}, // 2
  {1,1,1,1,0,0,1}, // 3
  {0,1,1,0,0,1,1}, // 4
  {1,0,1,1,0,1,1}, // 5
  {1,0,1,1,1,1,1}, // 6
  {1,1,1,0,0,0,0}, // 7
  {1,1,1,1,1,1,1}, // 8
  {1,1,1,1,0,1,1}  // 9
};

// Timing variables
unsigned long lastWeatherUpdate = 0;
unsigned long lastTimeUpdate = 0;
unsigned long lastParticleUpdate = 0;
const unsigned long WEATHER_UPDATE_INTERVAL = 600000; // 10 minutes
const unsigned long TIME_UPDATE_INTERVAL = 1000; // 1 second
const unsigned long PARTICLE_UPDATE_INTERVAL = 50; // 20 FPS

// Display colors
#define COLOR_SEGMENT_ON  ST7735_RED
#define COLOR_SEGMENT_OFF ST7735_BLACK
#define COLOR_BACKGROUND  ST7735_BLACK
#define COLOR_TEXT        ST7735_WHITE
#define COLOR_RAIN        ST7735_BLUE
#define COLOR_SNOW        ST7735_WHITE
#define COLOR_SUN         ST7735_YELLOW
#define COLOR_CLOUD       ST7735_CYAN

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("Initializing Weather Clock...");
  
  // Initialize display
  tft.initR(INITR_BLACKTAB);
  tft.setRotation(0);
  tft.fillScreen(COLOR_BACKGROUND);
  
  // Show startup message
  tft.setTextColor(COLOR_TEXT);
  tft.setTextSize(1);
  tft.setCursor(10, 60);
  tft.println("Weather Clock");
  tft.setCursor(20, 75);
  tft.println("Starting...");
  
  // Initialize WiFi
  connectToWiFi();
  
  // Initialize NTP client
  timeClient.begin();
  
  // Initialize particles
  initializeParticles();
  
  // Get initial weather data
  updateWeatherData();
  
  Serial.println("Setup complete!");
}

void loop() {
  unsigned long currentTime = millis();
  
  // Update time every second
  if (currentTime - lastTimeUpdate >= TIME_UPDATE_INTERVAL) {
    timeClient.update();
    lastTimeUpdate = currentTime;
  }
  
  // Update weather every 10 minutes
  if (currentTime - lastWeatherUpdate >= WEATHER_UPDATE_INTERVAL) {
    updateWeatherData();
    lastWeatherUpdate = currentTime;
  }
  
  // Update particles for weather effects
  if (currentTime - lastParticleUpdate >= PARTICLE_UPDATE_INTERVAL) {
    updateParticles();
    lastParticleUpdate = currentTime;
  }
  
  // Draw the display
  drawClock();
  
  delay(50); // Small delay to prevent overwhelming the system
}

void connectToWiFi() {
  tft.fillScreen(COLOR_BACKGROUND);
  tft.setCursor(10, 50);
  tft.println("Connecting WiFi...");
  
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(1000);
    Serial.print(".");
    attempts++;
    
    tft.setCursor(10, 70);
    tft.print("Attempt: ");
    tft.println(attempts);
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    Serial.println("IP address: " + WiFi.localIP().toString());
    
    tft.fillScreen(COLOR_BACKGROUND);
    tft.setCursor(10, 50);
    tft.println("WiFi Connected!");
    tft.setCursor(10, 65);
    tft.println(WiFi.localIP().toString());
    delay(2000);
  } else {
    Serial.println("\nWiFi connection failed!");
    tft.fillScreen(COLOR_BACKGROUND);
    tft.setCursor(10, 50);
    tft.println("WiFi Failed!");
    tft.setCursor(10, 65);
    tft.println("Check credentials");
  }
}

void updateWeatherData() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected, skipping weather update");
    return;
  }
  
  WiFiClient client;
  HTTPClient http;
  
  String url = WEATHER_BASE_URL + MUMBAI_CITY_ID + "&appid=" + WEATHER_API_KEY + "&units=metric";
  
  http.begin(client, url);
  int httpCode = http.GET();
  
  if (httpCode > 0) {
    String payload = http.getString();
    Serial.println("Weather API Response: " + payload);
    
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, payload);
    
    if (doc["cod"] == 200) {
      currentWeather.condition = doc["weather"][0]["main"].as<String>();
      currentWeather.description = doc["weather"][0]["description"].as<String>();
      currentWeather.temperature = doc["main"]["temp"];
      currentWeather.humidity = doc["main"]["humidity"];
      currentWeather.windSpeed = doc["wind"]["speed"];
      currentWeather.isValid = true;
      
      Serial.println("Weather updated: " + currentWeather.condition + 
                    ", " + String(currentWeather.temperature) + "°C");
    } else {
      Serial.println("Weather API error: " + String(doc["cod"].as<int>()));
      currentWeather.isValid = false;
    }
  } else {
    Serial.println("HTTP request failed: " + String(httpCode));
    currentWeather.isValid = false;
  }
  
  http.end();
}

void initializeParticles() {
  for (int i = 0; i < MAX_PARTICLES; i++) {
    particles[i].active = false;
    particles[i].life = 0;
  }
}

void updateParticles() {
  if (!currentWeather.isValid) return;
  
  // Create new particles based on weather condition
  if (currentWeather.condition == "Rain" || currentWeather.condition == "Drizzle") {
    createRainParticles();
  } else if (currentWeather.condition == "Snow") {
    createSnowParticles();
  } else if (currentWeather.condition == "Clear") {
    createSunParticles();
  } else if (currentWeather.condition == "Clouds") {
    createCloudParticles();
  }
  
  // Update existing particles
  for (int i = 0; i < MAX_PARTICLES; i++) {
    if (particles[i].active) {
      // Update position
      particles[i].x += particles[i].vx;
      particles[i].y += particles[i].vy;
      
      // Update life
      particles[i].life -= 0.02;
      
      // Remove particles that are off-screen or dead
      if (particles[i].life <= 0 || particles[i].y > 128 || 
          particles[i].x < 0 || particles[i].x > 128) {
        particles[i].active = false;
      }
    }
  }
}

void createRainParticles() {
  for (int i = 0; i < MAX_PARTICLES; i++) {
    if (!particles[i].active && random(100) < 30) {
      particles[i].x = random(128);
      particles[i].y = -5;
      particles[i].vx = random(-1, 2) * 0.5;
      particles[i].vy = random(2, 5);
      particles[i].life = 1.0;
      particles[i].color = COLOR_RAIN;
      particles[i].active = true;
      break;
    }
  }
}

void createSnowParticles() {
  for (int i = 0; i < MAX_PARTICLES; i++) {
    if (!particles[i].active && random(100) < 20) {
      particles[i].x = random(128);
      particles[i].y = -5;
      particles[i].vx = random(-2, 3) * 0.3;
      particles[i].vy = random(1, 3) * 0.5;
      particles[i].life = 1.0;
      particles[i].color = COLOR_SNOW;
      particles[i].active = true;
      break;
    }
  }
}

void createSunParticles() {
  for (int i = 0; i < MAX_PARTICLES; i++) {
    if (!particles[i].active && random(100) < 10) {
      particles[i].x = random(20, 108);
      particles[i].y = random(20, 40);
      particles[i].vx = random(-1, 2) * 0.2;
      particles[i].vy = random(-1, 2) * 0.2;
      particles[i].life = 0.5;
      particles[i].color = COLOR_SUN;
      particles[i].active = true;
      break;
    }
  }
}

void createCloudParticles() {
  for (int i = 0; i < MAX_PARTICLES; i++) {
    if (!particles[i].active && random(100) < 15) {
      particles[i].x = random(128);
      particles[i].y = random(10, 30);
      particles[i].vx = random(-2, 3) * 0.5;
      particles[i].vy = 0;
      particles[i].life = 1.0;
      particles[i].color = COLOR_CLOUD;
      particles[i].active = true;
      break;
    }
  }
}

void drawClock() {
  tft.fillScreen(COLOR_BACKGROUND);
  
  // Get current time
  int hours = timeClient.getHours();
  int minutes = timeClient.getMinutes();
  int seconds = timeClient.getSeconds();
  
  // Convert to 12-hour format
  bool isPM = hours >= 12;
  if (hours > 12) hours -= 12;
  if (hours == 0) hours = 12;
  
  // Draw seven-segment digits
  drawSevenSegmentDigit(hours / 10, 10, 45, 15);
  drawSevenSegmentDigit(hours % 10, 35, 45, 15);
  
  // Draw colon
  tft.fillCircle(55, 55, 2, COLOR_SEGMENT_ON);
  tft.fillCircle(55, 65, 2, COLOR_SEGMENT_ON);
  
  drawSevenSegmentDigit(minutes / 10, 65, 45, 15);
  drawSevenSegmentDigit(minutes % 10, 90, 45, 15);
  
  // Draw AM/PM indicator
  tft.setTextColor(COLOR_TEXT);
  tft.setTextSize(1);
  tft.setCursor(105, 50);
  tft.println(isPM ? "PM" : "AM");
  
  // Draw seconds in smaller format
  tft.setCursor(105, 65);
  tft.print(":");
  if (seconds < 10) tft.print("0");
  tft.println(seconds);
  
  // Draw weather information
  drawWeatherInfo();
  
  // Draw weather particles
  drawParticles();
  
  // Draw date
  drawDate();
}

void drawSevenSegmentDigit(int digit, int x, int y, int size) {
  if (digit < 0 || digit > 9) return;
  
  // Segment positions (relative to x, y)
  // a: top horizontal
  // b: top right vertical
  // c: bottom right vertical
  // d: bottom horizontal
  // e: bottom left vertical
  // f: top left vertical
  // g: middle horizontal
  
  int segWidth = size;
  int segHeight = size / 3;
  
  // Draw each segment based on the digit pattern
  if (digitPatterns[digit][0]) drawHorizontalSegment(x, y, segWidth, COLOR_SEGMENT_ON); // a
  if (digitPatterns[digit][1]) drawVerticalSegment(x + segWidth, y, segHeight, COLOR_SEGMENT_ON); // b
  if (digitPatterns[digit][2]) drawVerticalSegment(x + segWidth, y + segHeight + 2, segHeight, COLOR_SEGMENT_ON); // c
  if (digitPatterns[digit][3]) drawHorizontalSegment(x, y + 2 * segHeight + 2, segWidth, COLOR_SEGMENT_ON); // d
  if (digitPatterns[digit][4]) drawVerticalSegment(x, y + segHeight + 2, segHeight, COLOR_SEGMENT_ON); // e
  if (digitPatterns[digit][5]) drawVerticalSegment(x, y, segHeight, COLOR_SEGMENT_ON); // f
  if (digitPatterns[digit][6]) drawHorizontalSegment(x, y + segHeight + 1, segWidth, COLOR_SEGMENT_ON); // g
}

void drawHorizontalSegment(int x, int y, int width, uint16_t color) {
  tft.fillRect(x + 1, y, width - 2, 2, color);
}

void drawVerticalSegment(int x, int y, int height, uint16_t color) {
  tft.fillRect(x, y + 1, 2, height - 2, color);
}

void drawWeatherInfo() {
  if (!currentWeather.isValid) {
    tft.setTextColor(ST7735_RED);
    tft.setTextSize(1);
    tft.setCursor(5, 5);
    tft.println("Weather: N/A");
    return;
  }
  
  tft.setTextColor(COLOR_TEXT);
  tft.setTextSize(1);
  
  // Temperature
  tft.setCursor(5, 5);
  tft.print(currentWeather.temperature, 1);
  tft.print("C ");
  
  // Weather condition
  tft.print(currentWeather.condition);
  
  // Humidity on second line
  tft.setCursor(5, 15);
  tft.print("Humidity: ");
  tft.print(currentWeather.humidity);
  tft.print("%");
}

void drawParticles() {
  for (int i = 0; i < MAX_PARTICLES; i++) {
    if (particles[i].active) {
      // Fade color based on life
      uint16_t fadeColor = particles[i].color;
      if (particles[i].life < 0.5) {
        // Simple fade effect by drawing smaller pixels
        tft.drawPixel(particles[i].x, particles[i].y, fadeColor);
      } else {
        // Draw full particle
        if (currentWeather.condition == "Rain" || currentWeather.condition == "Drizzle") {
          tft.drawLine(particles[i].x, particles[i].y, 
                      particles[i].x, particles[i].y + 3, fadeColor);
        } else if (currentWeather.condition == "Snow") {
          tft.fillCircle(particles[i].x, particles[i].y, 1, fadeColor);
        } else {
          tft.drawPixel(particles[i].x, particles[i].y, fadeColor);
        }
      }
    }
  }
}

void drawDate() {
  time_t rawTime = timeClient.getEpochTime();
  struct tm * timeInfo = localtime(&rawTime);
  
  tft.setTextColor(COLOR_TEXT);
  tft.setTextSize(1);
  tft.setCursor(5, 110);
  
  // Day of week
  const char* days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  tft.print(days[timeInfo->tm_wday]);
  tft.print(" ");
  
  // Date
  tft.print(timeInfo->tm_mday);
  tft.print("/");
  tft.print(timeInfo->tm_mon + 1);
  tft.print("/");
  tft.print(timeInfo->tm_year + 1900);
  
  // Location
  tft.setCursor(5, 120);
  tft.println("Mumbai, India");
}