#ifndef WEATHER_API_H
#define WEATHER_API_H

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include "config.h"
#include "weather_data.h"
#include "secrets.h"

// Forward declarations
class ESP32Time;

class WeatherAPI {
public:
    WeatherAPI(ESP32Time& rtcRef); // Constructor takes ESP32Time reference

    // connectWiFi() removed - WiFi connection now handled in main.cpp
    bool setTime();
    bool getData(WeatherData& weatherData, DisplayState& displayState);

private:
    ESP32Time& rtc; // Reference to the global ESP32Time object

    // Helper functions
    void formatEpochToLocal(time_t epoch, char* out, size_t outSize, const char* fmt = "%H:%M");
};

#endif // WEATHER_API_H
