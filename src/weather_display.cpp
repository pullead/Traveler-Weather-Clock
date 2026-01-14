#include "weather_display.h"
#include <ESP32Time.h>

// Initialize static variables
char WeatherDisplay::timeBuffer[32];
char WeatherDisplay::valueStrBuffer[32];
char WeatherDisplay::counterStrBuffer[16];
unsigned long WeatherDisplay::frameCount = 0;
unsigned long WeatherDisplay::lastPerformanceReport = 0;
unsigned long WeatherDisplay::lastFrameTime = 0;

WeatherDisplay::WeatherDisplay(ESP32Time& rtcRef) : 
    tft(),
    sprite(&tft),
    errSprite(&tft),
    rtc(rtcRef), // Initialize the reference
    ani(ANIMATION_START_POSITION), 
    timePased(0), 
    displayBrightness(DEFAULT_BRIGHTNESS),
    lastButtonPress(0),
    temperature(22.2),
    messageUpdatePending(false),
    currentMessageWidth(0),
    messageWidthCached(false),
    currentFont(nullptr) {
    
    // Initialize legacy arrays with default values
    wData1[0] = 22.2;  // feels like
    wData1[1] = 25.0;   // cloud coverage
    wData1[2] = 10.0;   // visibility
    wData2[0] = 50.0;   // humidity
    wData2[1] = 1013.0; // pressure
    wData2[2] = 5.0;    // wind speed
    
    // Initialize message buffers with default weather message using units from config.h
    snprintf(Wmsg, sizeof(Wmsg), "... clear sky, visibility is 10.0%s, wind of 5.0%s, last updated at 12:00:00 ...", 
             PPlblU1_2, PPlblU2_2);
    strcpy(WmsgBuffer, Wmsg);
    
    setupUILabels();
    
    // Initialize scrolling message with default weather data
    updateScrollingMessage();
}

void WeatherDisplay::begin() {
    // Hardware initialization
    pinMode(POWER_PIN, OUTPUT);
    digitalWrite(POWER_PIN, HIGH);  // Power on the display
    
    // Additional power management for T-Display S3
    delay(100);  // Allow power to stabilize
    
    // Display initialization
    tft.init();
    tft.setRotation(1);
    tft.setSwapBytes(true);
    tft.fillScreen(TFT_BLACK);
    tft.drawString("Weather Micro Station", 30, 50, 4);
    
    // Create sprites for double buffering
    sprite.createSprite(SPRITE_WIDTH, SPRITE_HEIGHT);
    errSprite.createSprite(ERRSPRITE_WIDTH, ERRSPRITE_HEIGHT);
    
    // Configure display backlight
    ledcSetup(0, 10000, 8);
    ledcAttachPin(BACKLIGHT_PIN, 0);
    ledcWrite(0, DEFAULT_BRIGHTNESS);
    
    // Generate grayscale palette
    generateGrayscalePalette();
    
    // Initialize scrolling message with default weather data
    updateScrollingMessage();
    
    // Initialize brightness control buttons
    initializeBrightnessControl();
}

void WeatherDisplay::generateGrayscalePalette() {
    int colorValue = 210;
    for (int i = 0; i < GRAY_LEVELS; i++) {
        grays[i] = tft.color565(colorValue, colorValue, colorValue);
        colorValue -= 20;
    }
}

void WeatherDisplay::setupUILabels() {
    // Using defines from config.h for labels
    PPlbl1[0] = PPlbl1_0;
    PPlbl1[1] = PPlbl1_1;
    PPlbl1[2] = PPlbl1_2;
    
    // Using defines from config.h for units
    PPlblU1[0] = PPlblU1_0;
    PPlblU1[1] = PPlblU1_1;
    PPlblU1[2] = PPlblU1_2;
    
    // Using defines from config.h for labels
    PPlbl2[0] = PPlbl2_0;
    PPlbl2[1] = PPlbl2_1;
    PPlbl2[2] = PPlbl2_2;
    
    // Using defines from config.h for units
    PPlblU2[0] = PPlblU2_0;
    PPlblU2[1] = PPlblU2_1;
    PPlblU2[2] = PPlblU2_2;
}

void WeatherDisplay::initializeBrightnessControl() {
    // Configure button pins as inputs with pull-up resistors
    pinMode(BUTTON_BOOT, INPUT_PULLUP);
    pinMode(BUTTON_KEY, INPUT_PULLUP);
    
    displayBrightness = DEFAULT_BRIGHTNESS;
    
    Serial.printf("Brightness control initialized. Default brightness: %d\n", displayBrightness);
    Serial.println("Use Key button (GPIO14, top) to increase brightness, Boot button (GPIO0, bottom) to decrease");
}

void WeatherDisplay::handleBrightnessButtons() {
    unsigned long currentTime = millis();
    
    // Check if enough time has passed since last button press (debouncing)
    if (currentTime - lastButtonPress < BUTTON_DEBOUNCE_MS) {
        return;
    }
    
    bool buttonPressed = false;
    
    // Key button (GPIO14) - Increase brightness (top button)
    if (digitalRead(BUTTON_KEY) == LOW) {
        if (displayBrightness < 255) {
            displayBrightness = min(displayBrightness + BRIGHTNESS_STEP, 255);
            ledcWrite(0, displayBrightness);  // Use existing PWM channel 0
            Serial.printf("Brightness increased to: %d/255\n", displayBrightness);
            buttonPressed = true;
        }
    }
    
    // Boot button (GPIO0) - Decrease brightness (bottom button)
    if (digitalRead(BUTTON_BOOT) == LOW) {
        if (displayBrightness > 10) {  // Keep minimum brightness at 10 so display stays visible
            displayBrightness = max(displayBrightness - BRIGHTNESS_STEP, 10);
            ledcWrite(0, displayBrightness);  // Use existing PWM channel 0
            Serial.printf("Brightness decreased to: %d/255\n", displayBrightness);
            buttonPressed = true;
        }
    }
    
    if (buttonPressed) {
        lastButtonPress = currentTime;
    }
}

void WeatherDisplay::updateLegacyArrays() {
    // Update legacy arrays for compatibility
    wData1[0] = weatherData.feelsLike;
    wData1[1] = weatherData.cloudCoverage;
    wData1[2] = weatherData.visibility;
    wData2[0] = weatherData.humidity;
    wData2[1] = weatherData.pressure;
    wData2[2] = weatherData.windSpeed;

    // Update legacy variables
    temperature = weatherData.temperature;
    
}

void WeatherDisplay::updateLegacyData() {
    updateLegacyArrays();
}

// Performance optimization: Font management
void WeatherDisplay::loadFontOnce(const uint8_t* font) {
    if (currentFont != font) {
        if (currentFont != nullptr) {
            sprite.unloadFont();
        }
        sprite.loadFont(font);
        currentFont = font;
    }
}

void WeatherDisplay::unloadFontOnce() {
    if (currentFont != nullptr) {
        sprite.unloadFont();
        currentFont = nullptr;
    }
}

void WeatherDisplay::updateScrollingMessage() {
    // Create scrolling message using units from config.h
    // Format: "... description, visibility is (value)[unit], wind of (value)[unit], last updated at (time) ..."
    
    // Check for unlimited visibility (API max 10000m marked as -1)
    if (weatherData.visibility < 0) {
        snprintf(weatherData.scrollingMessage, sizeof(weatherData.scrollingMessage),
                "... %s, visibility is %s, wind of %.1f%s, last updated at %s ...",
                weatherData.description, VISIBILITY_UNLIMITED_FULL,
                weatherData.windSpeed, PPlblU2_2, weatherData.lastUpdated);
    } else {
        snprintf(weatherData.scrollingMessage, sizeof(weatherData.scrollingMessage),
                "... %s, visibility is %.1f%s, wind of %.1f%s, last updated at %s ...",
                weatherData.description, weatherData.visibility, PPlblU1_2,
                weatherData.windSpeed, PPlblU2_2, weatherData.lastUpdated);
    }
    
    Serial.printf("Scrolling: %s\n", weatherData.scrollingMessage);
    
    strcpy(WmsgBuffer, weatherData.scrollingMessage);
    messageUpdatePending = true;  // Mark that new message is ready
}

void WeatherDisplay::updateScrollingBuffer() {
    // Immediately update the display buffers with current scrolling message
    strcpy(Wmsg, weatherData.scrollingMessage);
    strcpy(WmsgBuffer, weatherData.scrollingMessage);
    messageUpdatePending = true;
    messageWidthCached = false; // Force recalculation of message width
}

void WeatherDisplay::updateData() {
    // Update scrolling animation - move 2 pixels per frame for better speed
    ani -= 2;
    
    // Reset position and update message at a fixed point for predictable transitions
    if (ani < ANIMATION_RESET_POSITION) {
        ani = ANIMATION_START_POSITION;
        
        // Apply pending message update AFTER position reset for smoother transition
        if (messageUpdatePending) {
            strcpy(Wmsg, WmsgBuffer);
            messageUpdatePending = false;
            currentMessageWidth = 0;  // Reset width so it gets recalculated in next draw()
            messageWidthCached = false;  // Mark width as needing recalculation
            Serial.printf("Scrolling message updated at animation restart: %s\n", Wmsg);
        }
    }
}

void WeatherDisplay::drawWeatherIcon(int x, int y, const char* iconCode) {
    const WeatherIcon* icon = getWeatherIcon(iconCode);
    if (icon != nullptr) {
        // Draw the icon pixel by pixel
        for (int py = 0; py < icon->height; py++) {
            for (int px = 0; px < icon->width; px++) {
                int pixelIndex = py * icon->width + px;
                uint16_t color = pgm_read_word(&icon->data[pixelIndex]);
                
                // Only draw non-black pixels (skip transparent areas)
                if (color != 0x0000) {
                    sprite.drawPixel(x + px, y + py, color);
                }
            }
        }
    }
}

void WeatherDisplay::drawLeftPanel() {
    // Header
    sprite.loadFont(midleFont);
    sprite.setTextColor(grays[1], TFT_BLACK);
    sprite.drawString("WEATHER", 6, 10);
    sprite.unloadFont();
    
    // City information
    sprite.loadFont(font18);
    sprite.setTextColor(grays[7], TFT_BLACK);
    sprite.drawString("CITY:", 6, 110);
    sprite.setTextColor(grays[3], TFT_BLACK);
    sprite.drawString(config.city, 48, 110);
    sprite.unloadFont();
    
    // Main temperature display
    sprite.setTextDatum(4);  // Center alignment for temperature
    sprite.loadFont(bigFont);
    sprite.setTextColor(grays[0], TFT_BLACK);
    sprite.drawFloat(weatherData.temperature, 1, 50, 80);
    sprite.unloadFont();
    
    // Temperature unit indicator (uses USE_METRIC_UNITS from config.h)
    sprite.loadFont(font18);
    sprite.setTextColor(grays[2], TFT_BLACK);
    #if USE_METRIC_UNITS
        sprite.drawString("C", 112, 55);
        sprite.fillCircle(103, 50, 2, grays[2]);  // Degree symbol
    #else
        sprite.drawString("F", 112, 49);
        sprite.fillCircle(103, 50, 2, grays[2]);  // Degree symbol
    #endif
    sprite.unloadFont();
    
    // Time display - memory efficient implementation using static buffer
    strcpy(timeBuffer, rtc.getTime().c_str());
    
    // Extract hours:minutes (first 5 characters)
    char timeHM[6];
    strncpy(timeHM, timeBuffer, 5);
    timeHM[5] = '\0';
    
    // Extract seconds (characters 6-7)
    char timeSS[3];
    strncpy(timeSS, timeBuffer + 6, 2);
    timeSS[2] = '\0';
    
    // Time without seconds (HH:MM)
    sprite.setTextDatum(0);  // Left alignment
    sprite.loadFont(tinyFont);
    sprite.setTextColor(grays[4], TFT_BLACK);
    sprite.drawString(timeHM, 6, 132);
    sprite.unloadFont();
    
    // Seconds in highlighted rectangle
    sprite.fillRoundRect(90, 132, 42, 22, 2, grays[2]);
    sprite.loadFont(font18);
    sprite.setTextColor(TFT_BLACK, grays[2]);
    sprite.setTextDatum(4);  // Center alignment
    sprite.drawString(timeSS, 111, 144);
    sprite.unloadFont();
    
    // "SECONDS" label
    sprite.setTextDatum(0);
    sprite.setTextColor(grays[5], TFT_BLACK);
    sprite.drawString("SECONDS", 91, 157);
    
    // Icon placeholder area - keeping your original "ICON HERE" text
    sprite.setTextColor(grays[5], TFT_BLACK);
    sprite.drawString("MICRO", 88, 10);
    sprite.drawString("STATION", 88, 20);
}

void WeatherDisplay::drawRightPanel() {
    // Sunrise and sunset information
    sprite.setTextDatum(0);  // Left alignment
    sprite.loadFont(font18);
    sprite.setTextColor(grays[1], TFT_BLACK);
    sprite.drawString("sunrise:", 144, 10);
    sprite.drawString("sunset:", 144, 28);
    
    sprite.setTextColor(grays[3], TFT_BLACK);
    sprite.drawString(weatherData.sunriseTime, 210, 12);
    sprite.drawString(weatherData.sunsetTime, 210, 30);
    sprite.unloadFont();
    
    // Draw weather icon next to sunrise/sunset times
    if (strlen(weatherData.weatherIcon) > 0) {
        drawWeatherIcon(278, 12, weatherData.weatherIcon);
    }
    
    // Weather data boxes - top row
    for (int i = 0; i < 3; i++) {
        int x = 144 + (i * 60);
        sprite.fillSmoothRoundRect(x, 53, 54, 32, 3, grays[9], TFT_BLACK);
        sprite.setTextDatum(4);  // Center alignment
        sprite.setTextColor(grays[3], grays[9]);
        sprite.drawString(PPlbl1[i], x + 27, 59);
        sprite.setTextColor(grays[2], grays[9]);
        sprite.loadFont(font18);
        // Special formatting based on data type
        if (i == 0) {
            // Feels like temperature - show 1 decimal place
            snprintf(valueStrBuffer, sizeof(valueStrBuffer), "%.1f%s", wData1[i], PPlblU1[i]);
        } else if (i == 2 && wData1[i] < 0) {
            // Visibility - check for unlimited marker (API max 10000m)
            snprintf(valueStrBuffer, sizeof(valueStrBuffer), "%s%s", VISIBILITY_UNLIMITED, PPlblU1[i]);
        } else {
            snprintf(valueStrBuffer, sizeof(valueStrBuffer), "%.0f%s", wData1[i], PPlblU1[i]);
        }
        sprite.drawString(valueStrBuffer, x + 27, 76);
        sprite.unloadFont();
    }
    
    // Weather data boxes - bottom row
    for (int i = 0; i < 3; i++) {
        int x = 144 + (i * 60);
        sprite.fillSmoothRoundRect(x, 93, 54, 32, 3, grays[9], TFT_BLACK);
        sprite.setTextDatum(4);  // Center alignment
        sprite.setTextColor(grays[3], grays[9]);
        sprite.drawString(PPlbl2[i], x + 27, 99);
        sprite.setTextColor(grays[2], grays[9]);
        sprite.loadFont(font18);
        snprintf(valueStrBuffer, sizeof(valueStrBuffer), "%.0f%s", wData2[i], PPlblU2[i]);
        sprite.drawString(valueStrBuffer, x + 27, 116);
        sprite.unloadFont();
    }
    
    // Scrolling message area
    sprite.fillSmoothRoundRect(144, 148, 174, 16, 2, grays[10], TFT_BLACK);
    errSprite.pushToSprite(&sprite, 148, 150);
    
    // Status information - use font18 (continues from weather data boxes)
    sprite.setTextDatum(0);  // Left alignment
    sprite.setTextColor(grays[4], TFT_BLACK);
    sprite.drawString("CURRENT CONDITIONS", 145, 138);
    sprite.setTextColor(grays[9], TFT_BLACK);
    snprintf(counterStrBuffer, sizeof(counterStrBuffer), "%d", displayState.updateCounter);
    sprite.drawString(counterStrBuffer, 310, 141);
}

void WeatherDisplay::draw() {
    
    // Prepare scrolling message with seamless looping
    errSprite.fillSprite(grays[10]);
    errSprite.setTextColor(grays[1], grays[10]);
    
    // Calculate message width for seamless scrolling (only when needed)
    errSprite.setTextDatum(0);  // Left alignment
    if (!messageWidthCached || currentMessageWidth == 0) {
        currentMessageWidth = errSprite.textWidth(Wmsg);
        messageWidthCached = true;
    }
    int totalWidth = currentMessageWidth + SCROLL_SPACING;
    
    // Only draw the message once at the start of a new cycle to avoid mid-transition issues
    if (ani >= 0) {
        // Normal scrolling - draw two copies for seamless loop
        errSprite.drawString(Wmsg, ani, 4);
        errSprite.drawString(Wmsg, ani + totalWidth, 4);
    } else {
        // During off-screen phase - only draw the copy that might be visible
        errSprite.drawString(Wmsg, ani, 4);
        if (ani + totalWidth > -currentMessageWidth) {
            errSprite.drawString(Wmsg, ani + totalWidth, 4);
        }
    }
    
    // Clear main sprite and draw divider lines
    sprite.fillSprite(TFT_BLACK);
    sprite.drawLine(138, 10, 138, 164, grays[6]);  // Vertical divider
    sprite.drawLine(100, 108, 134, 108, grays[6]); // Horizontal divider in left panel
    sprite.setTextDatum(0);  // Reset text alignment
    
    // Draw main panels
    drawLeftPanel();
    drawRightPanel();
    
    // Push sprite to display
    sprite.pushSprite(0, 0);
    
    // Performance monitoring
    frameCount++;
    unsigned long currentTime = millis();
    if (currentTime - lastPerformanceReport >= 10000) {  // Every 10 seconds
        reportPerformanceStats();
        lastPerformanceReport = currentTime;
    }
    lastFrameTime = currentTime;
}

// Performance monitoring implementation
void WeatherDisplay::reportPerformanceStats() {
    float fps = frameCount / 10.0f;  // Frames per second over last 10 seconds
    Serial.printf("Performance: FPS=%.1f, Free Heap=%d bytes, Frame Count=%lu\n", 
                 fps, ESP.getFreeHeap(), frameCount);
    frameCount = 0;  // Reset counter
}
