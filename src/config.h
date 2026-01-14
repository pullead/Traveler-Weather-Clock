#ifndef CONFIG_H
#define CONFIG_H

// ==================== DISPLAY CONFIGURATION ====================
#define SPRITE_WIDTH 320
#define SPRITE_HEIGHT 170
#define ERRSPRITE_WIDTH 164
#define ERRSPRITE_HEIGHT 15
#define BACKLIGHT_PIN 38
#define POWER_PIN 15
#define DEFAULT_BRIGHTNESS 215
#define GRAY_LEVELS 13

// ==================== WEATHER CONFIGURATION ====================
#define UPDATE_INTERVAL_MS 180000  // 3 minutes - respects API rate limits
#define SYNC_INTERVAL_UPDATES 10   // Sync time every 10 updates (30 minutes)
#define MAX_RETRY_ATTEMPTS 3
#define RETRY_DELAY_MS 5000
#define ANIMATION_RESET_POSITION -400
#define ANIMATION_START_POSITION 100
#define SCROLL_SPACING 80          // Space between repeated scrolling messages
#define TEMPERATURE_HISTORY_SIZE 24

// ==================== API CONFIGURATION ====================
#define API_RESPONSE_BUFFER_SIZE 2048  // Max API response size in bytes
#define API_TIMEOUT_MS 10000           // HTTP request timeout

// ==================== NETWORK CONFIGURATION ====================
#define NTP_SERVER "pool.ntp.org"
#define WIFI_TIMEOUT_MS 5000

// ==================== TIMEZONE CONFIGURATION ====================
// Change these values for your timezone:
//
// Common examples:
//   Eastern US:  GMT_OFFSET = -5,  DST = 1,  TZ = "EST5EDT,M3.2.0/2,M11.1.0/2"
//   Central US:  GMT_OFFSET = -6,  DST = 1,  TZ = "CST6CDT,M3.2.0/2,M11.1.0/2"
//   Pacific US:  GMT_OFFSET = -8,  DST = 1,  TZ = "PST8PDT,M3.2.0/2,M11.1.0/2"
//   UK/London:   GMT_OFFSET = 0,   DST = 1,  TZ = "GMT0BST,M3.5.0/1,M10.5.0/2"
//   Europe:      GMT_OFFSET = 1,   DST = 1,  TZ = "CET-1CEST,M3.5.0,M10.5.0/3"
//   Japan:       GMT_OFFSET = 9,   DST = 0,  TZ = "JST-9"
//   Australia:   GMT_OFFSET = 10,  DST = 1,  TZ = "AEST-10AEDT,M10.1.0,M4.1.0/3"
//
#define GMT_OFFSET_HOURS -5                    // Your UTC offset in hours
#define DAYLIGHT_SAVING_ENABLED 1              // 1 = DST observed, 0 = no DST
#define TIMEZONE_STRING "EST5EDT,M3.2.0/2,M11.1.0/2"  // POSIX TZ string

// Calculated values (don't modify)
#define GMT_OFFSET_SEC (GMT_OFFSET_HOURS * 3600)
#define DAYLIGHT_OFFSET_SEC (DAYLIGHT_SAVING_ENABLED * 3600)

// ==================== BUTTON CONFIGURATION ====================
#define BUTTON_BOOT 0      // GPIO0 - Boot button (brightness down - bottom button)
#define BUTTON_KEY 14      // GPIO14 - Key button (brightness up - top button)
#define BRIGHTNESS_STEP 25 // Step size for brightness changes
#define BUTTON_DEBOUNCE_MS 200  // Debounce delay

// ==================== DISPLAY COLORS ====================
#define TFT_BLACK 0x0000
#define TFT_WHITE 0xFFFF

// ==================== UNIT SYSTEM CONFIGURATION ====================
// Set to true for metric (°C, km/h, km), false for imperial (°F, mph, mi)
#define USE_METRIC_UNITS true

// Unit system string for API calls
#if USE_METRIC_UNITS
    #define UNIT_SYSTEM "metric"
#else
    #define UNIT_SYSTEM "imperial"
#endif

// ==================== UI LABELS ====================
// Static UI labels for weather data display (same for both unit systems)
#define PPlbl1_0 "FEELS"
#define PPlbl1_1 "CLOUDS" 
#define PPlbl1_2 "VISIBIL."

#define PPlbl2_0 "HUMIDITY"
#define PPlbl2_1 "PRESSURE"
#define PPlbl2_2 "WIND"

// ==================== UNIT LABELS (Conditional) ====================
// Units change based on USE_METRIC_UNITS setting
#if USE_METRIC_UNITS
    // Metric units
    #define PPlblU1_0 " °C"      // Temperature (Feels like)
    #define PPlblU1_1 " %"       // Cloud coverage (same for both)
    #define PPlblU1_2 " km"      // Visibility
    #define PPlblU2_0 " %"       // Humidity (same for both)
    #define PPlblU2_1 " hPa"     // Pressure (same for both)
    #define PPlblU2_2 " km/h"    // Wind speed
    // Unlimited visibility display (API max is 10000m = 10km)
    #define VISIBILITY_UNLIMITED "10+"
    #define VISIBILITY_UNLIMITED_FULL "10+ km"
#else
    // Imperial units
    #define PPlblU1_0 " °F"      // Temperature (Feels like)
    #define PPlblU1_1 " %"       // Cloud coverage (same for both)
    #define PPlblU1_2 " mi"      // Visibility
    #define PPlblU2_0 " %"       // Humidity (same for both)
    #define PPlblU2_1 " hPa"     // Pressure (same for both)
    #define PPlblU2_2 " mph"     // Wind speed
    // Unlimited visibility display (API max is 10000m ≈ 6.2mi)
    #define VISIBILITY_UNLIMITED "6+"
    #define VISIBILITY_UNLIMITED_FULL "6+ mi"
#endif

// Special marker for unlimited visibility (-1 indicates API returned 10000m max)
#define VISIBILITY_UNLIMITED_MARKER -1

#endif // CONFIG_H
