# ESP8266 Weather Clock with Seven-Segment Display

A sophisticated Arduino sketch for the WeMos D1 R1 (ESP8266) that displays time in seven-segment format on a 1.44" TFT SPI display with weather-based physics animations.

## Features

- **Seven-segment digital clock** with 12-hour format (AM/PM)
- **IST (Indian Standard Time)** synchronization via NTP
- **Real-time weather data** from OpenWeatherMap API for Mumbai
- **Physics-based weather animations**:
  - Rain: Falling blue droplets with wind effects
  - Snow: Gentle white snowflakes with drift
  - Clear: Sparkling sun particles
  - Clouds: Drifting cloud particles
- **Robust error handling** and WiFi reconnection
- **Date display** with day of week
- **Temperature and humidity** display

## Hardware Requirements

- **WeMos D1 R1** board (ESP8266 WROOM)
- **1.44" TFT SPI Display** (128x128, ST7735 controller)
- **Wiring** as per pin definitions:
  ```
  TFT_CS   -> D2 (GPIO4)
  TFT_RST  -> D3 (GPIO0)
  TFT_DC   -> D4 (GPIO2)
  TFT_MOSI -> D7 (GPIO13)
  TFT_SCLK -> D5 (GPIO14)
  VCC      -> 3.3V
  GND      -> GND
  ```

## Required Libraries

Install these libraries through the Arduino IDE Library Manager:

1. **ESP8266WiFi** (Built-in with ESP8266 board package)
2. **ESP8266HTTPClient** (Built-in with ESP8266 board package)
3. **ArduinoJson** by Benoit Blanchon (v6.x)
4. **Adafruit GFX Library** by Adafruit
5. **Adafruit ST7735 and ST7789 Library** by Adafruit
6. **NTPClient** by Fabrice Weinberg
7. **Time** by Paul Stoffregen

## Setup Instructions

### 1. Install ESP8266 Board Package
- In Arduino IDE, go to File → Preferences
- Add this URL to "Additional Board Manager URLs":
  ```
  http://arduino.esp8266.com/stable/package_esp8266com_index.json
  ```
- Go to Tools → Board → Boards Manager
- Search for "ESP8266" and install the package

### 2. Configure WiFi
Update these lines in the code with your WiFi credentials:
```cpp
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
```

### 3. Weather API Setup
The code includes a pre-configured OpenWeatherMap API key for Mumbai. If you want to use your own:
1. Sign up at [OpenWeatherMap](https://openweathermap.org/api)
2. Get your free API key
3. Update the `WEATHER_API_KEY` in the code

### 4. Board Configuration
- Select **Board**: "LOLIN(WEMOS) D1 R2 & mini" or "NodeMCU 1.0"
- Select **Port**: Your ESP8266's COM port
- **Upload Speed**: 115200
- **Flash Size**: 4M (3M SPIFFS)

## Code Structure

### Main Components

- **Seven-Segment Display**: Custom digit rendering with segment patterns
- **Weather Integration**: Real-time data from OpenWeatherMap API
- **Physics Engine**: Particle system for weather animations
- **Time Management**: NTP synchronization with IST timezone
- **Display Management**: Optimized rendering for 128x128 TFT

### Key Functions

- `drawSevenSegmentDigit()`: Renders individual digits
- `updateWeatherData()`: Fetches weather from API
- `updateParticles()`: Physics simulation for weather effects
- `drawClock()`: Main display rendering loop

## Weather Effects

- **Rain/Drizzle**: Blue droplets falling with random wind drift
- **Snow**: White snowflakes with gentle floating motion
- **Clear**: Yellow sparkles representing sunshine
- **Clouds**: Cyan particles drifting horizontally

## Troubleshooting

### Common Issues

1. **Display not working**:
   - Check wiring connections
   - Verify TFT library compatibility
   - Try different display initialization modes

2. **WiFi connection fails**:
   - Verify SSID and password
   - Check WiFi signal strength
   - Ensure 2.4GHz network (ESP8266 doesn't support 5GHz)

3. **Weather data not updating**:
   - Check internet connection
   - Verify API key validity
   - Monitor Serial output for error messages

4. **Time not syncing**:
   - Ensure NTP servers are accessible
   - Check timezone offset (19800 seconds for IST)

### Serial Monitor Output

Enable Serial Monitor (115200 baud) to see:
- WiFi connection status
- Weather API responses
- Time synchronization
- Error messages

## Customization

### Changing Location
To display weather for a different city:
1. Find your city ID at [OpenWeatherMap](https://openweathermap.org/find)
2. Update `MUMBAI_CITY_ID` with your city ID
3. Update the location text in `drawDate()` function

### Adjusting Colors
Modify the color definitions:
```cpp
#define COLOR_SEGMENT_ON  ST7735_RED    // Seven-segment color
#define COLOR_RAIN        ST7735_BLUE   // Rain particle color
#define COLOR_SNOW        ST7735_WHITE  // Snow particle color
// ... etc
```

### Time Format
To switch to 24-hour format, modify the time conversion logic in `drawClock()`.

## Performance Notes

- Weather updates every 10 minutes to respect API limits
- Particle system limited to 20 particles for smooth animation
- Display refresh optimized for ESP8266's processing capabilities
- Memory usage kept minimal for stable operation

## License

This project is open source. Feel free to modify and distribute.

## Credits

- Weather data provided by [OpenWeatherMap](https://openweathermap.org/)
- Built with Arduino IDE and ESP8266 libraries
- Designed for WeMos D1 R1 and ST7735 TFT displays
