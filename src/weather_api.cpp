#include "weather_api.h"
#include <ESP32Time.h>

WeatherAPI::WeatherAPI(ESP32Time& rtcRef) : rtc(rtcRef) {
    // Constructor - rtc reference initialized
}

// connectWiFi() removed - WiFi connection now handled in main.cpp

bool WeatherAPI::setTime() {
    Serial.println("Synchronizing time with NTP server...");
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
    
    struct tm timeinfo;
    int attempts = 0;
    const int maxAttempts = 10;
    
    while (attempts < maxAttempts) {
        if (getLocalTime(&timeinfo)) {
            Serial.println("Time synchronized successfully");
            return true;
        }
        delay(1000);
        attempts++;
    }
    
    Serial.println("Failed to sync time with NTP");
    return false;
}

bool WeatherAPI::getData(WeatherData& weatherData, DisplayState& displayState) {
    Serial.printf("=== FETCHING WEATHER DATA [%lu ms] ===\n", millis());
    Serial.printf("API URL: %s\n", OPENWEATHERMAP_API_ENDPOINT);
    
    // Use full API endpoint from secrets.h
    const char* apiUrl = OPENWEATHERMAP_API_ENDPOINT;
    
    HTTPClient http;
    http.begin(apiUrl);
    http.setTimeout(API_TIMEOUT_MS);
    
    Serial.println("Fetching weather data from API...");
    int httpResponseCode = http.GET();
    
    if (httpResponseCode > 0) {
        // Use more memory-efficient approach than String
        int payloadSize = http.getSize();
        if (payloadSize > API_RESPONSE_BUFFER_SIZE) {
            Serial.println("Response too large");
            http.end();
            return false;
        }
        
        // Read directly into char buffer to avoid String heap allocation
        char payload[API_RESPONSE_BUFFER_SIZE];
        WiFiClient* stream = http.getStreamPtr();
        int bytesRead = stream->readBytes(payload, min(payloadSize, (int)sizeof(payload) - 1));
        payload[bytesRead] = '\0';
        
        Serial.println("API response received successfully");
        
        // Parse JSON response with error handling
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, payload);
        
        if (!error) {
            // Validate required fields exist
            if (!doc["main"]["temp"] || !doc["weather"][0]["description"]) {
                Serial.println("Missing required fields in API response");
                http.end();
                return false;
            }
            
            // Extract and validate data
            weatherData.temperature = doc["main"]["temp"];
            weatherData.feelsLike = doc["main"]["feels_like"];
            weatherData.humidity = doc["main"]["humidity"];
            weatherData.pressure = doc["main"]["pressure"];
            weatherData.windSpeed = doc["wind"]["speed"];
            weatherData.cloudCoverage = doc["clouds"]["all"];
            weatherData.visibility = doc["visibility"];
            
            // Check for max visibility (10000m = unlimited/clear conditions)
            bool unlimitedVisibility = (weatherData.visibility >= 10000);
            
            // Convert units based on unit system (from config.h)
            // Note: OpenWeatherMap API wind speed:
            //   - metric: returns m/s (we convert to km/h)
            //   - imperial: returns mph (no conversion needed)
            // Note: Visibility is ALWAYS in meters regardless of unit setting
            #if USE_METRIC_UNITS
                weatherData.windSpeed = weatherData.windSpeed * 3.6;       // m/s → km/h
                weatherData.visibility = weatherData.visibility / 1000.0;  // meters → km
            #else
                // Imperial: wind already in mph from API, no conversion
                weatherData.visibility = weatherData.visibility / 1609.34; // meters → miles
            #endif
            
            // Mark unlimited visibility with special value (will show as "10+" or "6+")
            if (unlimitedVisibility) {
                weatherData.visibility = VISIBILITY_UNLIMITED_MARKER;
            }
            
            // Copy description safely
            const char* desc = doc["weather"][0]["description"];
            strncpy(weatherData.description, desc, sizeof(weatherData.description) - 1);
            weatherData.description[sizeof(weatherData.description) - 1] = '\0';
            
            // Set last updated to current local time when API fetch happened
            time_t now = time(nullptr);
            struct tm* timeinfo = localtime(&now);
            if (timeinfo != nullptr) {
                strftime(weatherData.lastUpdated, sizeof(weatherData.lastUpdated), "%H:%M:%S", timeinfo);
            } else {
                strcpy(weatherData.lastUpdated, "12:00:00");
            }
            
            // Extract weather icon code
            const char* icon = doc["weather"][0]["icon"];
            if (icon) {
                strncpy(weatherData.weatherIcon, icon, sizeof(weatherData.weatherIcon) - 1);
                weatherData.weatherIcon[sizeof(weatherData.weatherIcon) - 1] = '\0';
            }
            
            // Process sunrise/sunset times
            long sunrise = doc["sys"]["sunrise"];
            long sunset = doc["sys"]["sunset"];
            formatEpochToLocal(sunrise, weatherData.sunriseTime, sizeof(weatherData.sunriseTime), "%H:%M:%S");
            formatEpochToLocal(sunset, weatherData.sunsetTime, sizeof(weatherData.sunsetTime), "%H:%M:%S");
            
            // Simple API data output (using unit labels from config.h)
            Serial.println("API VALUES:");
            Serial.printf("Temp: %.1f%s | Feels: %.1f%s | Humidity: %.0f%s | Pressure: %.0f%s\n", 
                         weatherData.temperature, PPlblU1_0,
                         weatherData.feelsLike, PPlblU1_0,
                         weatherData.humidity, PPlblU2_0,
                         weatherData.pressure, PPlblU2_1);
            // Handle unlimited visibility in debug output
            if (weatherData.visibility < 0) {
                Serial.printf("Wind: %.1f%s | Clouds: %.0f%s | Visibility: %s | %s\n", 
                             weatherData.windSpeed, PPlblU2_2,
                             weatherData.cloudCoverage, PPlblU1_1,
                             VISIBILITY_UNLIMITED_FULL,
                             weatherData.description);
            } else {
                Serial.printf("Wind: %.1f%s | Clouds: %.0f%s | Visibility: %.1f%s | %s\n", 
                             weatherData.windSpeed, PPlblU2_2,
                             weatherData.cloudCoverage, PPlblU1_1,
                             weatherData.visibility, PPlblU1_2,
                             weatherData.description);
            }
            Serial.printf("Updated: %s | Units: %s\n", weatherData.lastUpdated, UNIT_SYSTEM);
            Serial.println("=== API FETCH SUCCESS ===");
            
            displayState.isConnected = true;
            http.end();
            return true;
            
        } else {
            Serial.println("ERROR: Failed to parse JSON response");
        }
    } else {
        Serial.printf("ERROR: HTTP request failed with code: %d\n", httpResponseCode);
    }
    
    http.end();
    displayState.isConnected = false;
    Serial.println("=== API FETCH FAILED ===");
    return false;
}

void WeatherAPI::formatEpochToLocal(time_t epoch, char* out, size_t outSize, const char* fmt) {
    // Use timezone from config.h
    setenv("TZ", TIMEZONE_STRING, 1);
    tzset();

    struct tm tmLocal;
    localtime_r(&epoch, &tmLocal);     // convert to local time
    strftime(out, outSize, fmt, &tmLocal); // format to "HH:MM:SS EDT" by default
}

