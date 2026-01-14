#ifndef WEATHER_DATA_H
#define WEATHER_DATA_H

#include "config.h"
#include "secrets.h"

// ==================== WEATHER CONFIGURATION STRUCTURE ====================
// Simplified: Only stores city for display purposes
// API key and units are handled via defines in secrets.h and config.h
struct WeatherConfig {
    char city[32];
    
    WeatherConfig() {
        strcpy(city, OPENWEATHERMAP_CITY);  // City from secrets.h
    }
};

// ==================== WEATHER DATA STRUCTURE ====================
struct WeatherData {
    float temperature;
    float feelsLike;
    float humidity;
    float pressure;
    float windSpeed;
    float cloudCoverage;
    float visibility;
    char description[64];
    char weatherIcon[8];  // Weather icon code (e.g., "01d", "02n")
    char sunriseTime[16];
    char sunsetTime[16];
    char scrollingMessage[512];  // Increased buffer size for longer messages
    char lastUpdated[32];       // Last updated datetime from API
    float minTemp;
    float maxTemp;
    
    // Constructor with default values
    WeatherData() : temperature(22.2), feelsLike(22.2), humidity(50), pressure(1013), 
                   windSpeed(5.0), cloudCoverage(25), visibility(10), minTemp(-50), maxTemp(1000) {
        strcpy(description, "clear sky");
        strcpy(weatherIcon, "01d");  // Default clear sky day icon
        strcpy(sunriseTime, "--:--");
        strcpy(sunsetTime, "--:--");
        strcpy(scrollingMessage, "Initializing weather data...");
        strcpy(lastUpdated, "12:00:00");
    }
};

// ==================== DISPLAY STATE STRUCTURE ====================
struct DisplayState {
    int animationOffset;
    unsigned long lastUpdateTime;
    int updateCounter;
    bool isConnected;
    bool hasError;
    char errorMessage[128];
    
    DisplayState() : animationOffset(ANIMATION_START_POSITION), 
                    lastUpdateTime(0), updateCounter(0), isConnected(false), hasError(false) {
        strcpy(errorMessage, "");
    }
};

#endif // WEATHER_DATA_H
