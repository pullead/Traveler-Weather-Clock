#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <SPI.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiManager.h>
#include <WiFiMulti.h>
#include <WebServer.h>
#include <time.h>

#include "art_assets.h"
#include "bichon_asset.h"
#include "camp_asset.h"
#include "startup_logo_asset.h"
#include "admin_ui.h"

namespace {

constexpr uint8_t BOOT_BUTTON_PIN = 0;
constexpr uint32_t FRAME_US = 1000000UL / 24UL;
constexpr uint16_t CROSSFADE_FRAMES = 48;
constexpr uint16_t BACKGROUND_STEP_Q8 = 392;  // 1.53125 px/frame = 36.75 px/s.
constexpr uint16_t RIDER_STEP_Q8 = 78;        // About 7.3 poses/s; 3.3 s per cycle.
constexpr uint16_t DOG_STEP_Q8 = 108;         // About 10 key poses/s with fractional crossfades.
constexpr uint32_t WEATHER_REFRESH_MS = 10UL * 60UL * 1000UL;
constexpr uint8_t PRECIPITATION_HOURS = 24;
constexpr uint8_t FORECAST_DAYS = 7;
constexpr uint8_t MAX_KNOWN_WIFI_NETWORKS = 8;
constexpr uint8_t MAX_BIRD_PASSES_PER_HOUR = 5;
constexpr uint8_t MAX_BIRDS_PER_FLOCK = 4;
constexpr float LATITUDE = 35.3233F;
constexpr float LONGITUDE = 134.8483F;
constexpr char TIMEZONE[] = "JST-9";
constexpr char DEVICE_NAME[] = "Traveler Weather Clock";
constexpr char FIRMWARE_VERSION[] = "v1.4.0";
constexpr char WEB_UI_VERSION[] = "v1.0.0";

constexpr uint16_t BLACK = 0x0000;
constexpr uint16_t UI_NAVY = 0x000E;
constexpr uint16_t WHITE = 0xFFFF;
constexpr uint16_t MUTED = 0xBDF7;
constexpr uint16_t CYAN = 0x6E7F;
constexpr uint16_t WARM = 0xFE26;
constexpr uint16_t RED = 0xF986;
constexpr uint16_t RAIN_BLUE = 0x8E9F;
constexpr uint16_t RAIN_MID = 0x6D9B;
constexpr uint16_t RAIN_FAINT = 0x4C75;

Adafruit_ST7789 display(TFT_CS, TFT_DC, TFT_RST);
GFXcanvas16 frameBuffer(SCENE_WIDTH, SCENE_HEIGHT);
U8G2_FOR_ADAFRUIT_GFX zh;
WiFiManager wifiManager;
WiFiMulti knownNetworks;
WebServer adminServer(80);
uint8_t knownNetworkCount = 0;

struct WeatherState {
  float temperature = NAN;
  float apparentTemperature = NAN;
  float dailyMin = NAN;
  float dailyMax = NAN;
  float precipitation = 0.0F;
  float rain = 0.0F;
  float snowfall = 0.0F;
  float cloudCover = 0.0F;
  float windSpeed = 0.0F;
  float windGust = 0.0F;
  float visibility = NAN;
  float pm10 = NAN;
  float dust = NAN;
  float aerosolOpticalDepth = NAN;
  uint8_t precipitationProbability[PRECIPITATION_HOURS] = {};
  uint8_t precipitationProbabilityCount = 0;
  float forecastMin[FORECAST_DAYS] = {};
  float forecastMax[FORECAST_DAYS] = {};
  uint8_t forecastPrecipitation[FORECAST_DAYS] = {};
  int forecastCode[FORECAST_DAYS] = {};
  uint8_t forecastCount = 0;
  int sunriseMinutes = 360;
  int sunsetMinutes = 1080;
  int code = -1;
  int nextCode = -1;
  bool valid = false;
};

struct AdminSettings {
  uint8_t outfitMode = 0;      // 0 auto, 1 manual.
  uint8_t manualOutfit = 5;    // Raincoat in OutfitKind.
  uint8_t brightness = 100;
  uint8_t animationSpeed = 100;
  uint8_t riderSpeed = 100;
  uint8_t backgroundSpeed = 100;
  uint8_t dogSpeed = 100;
  uint8_t dogTint = 0;
  uint8_t dogNightBright = 70;
  uint8_t theme = 0;
  bool showWifi = true;
  bool showSunSchedule = true;
  bool showPrecipitation = true;
  bool showFeelsLike = true;
  bool showMoon = true;
  bool showHoliday = true;
  bool showTemperature = true;
  bool showDog = true;
  bool showBirds = true;
  bool showWeatherEffects = true;
};

WeatherState weather;
AdminSettings adminSettings;
portMUX_TYPE weatherMux = portMUX_INITIALIZER_UNLOCKED;
volatile bool requestWeather = true;
volatile bool holidayToday = false;
uint32_t lastWeatherFetch = 0;

enum SceneKind : uint8_t { DAY, SUNSET, NIGHT, RAIN, SNOW };
SceneKind currentScene = DAY;
SceneKind previousScene = DAY;
uint16_t crossfadeFrame = CROSSFADE_FRAMES;
uint8_t previewMode = 0;  // 0 auto, then day/sunset/night/rain/snow.

uint32_t animationFrame = 0;
uint32_t backgroundScrollQ8 = 0;
uint32_t riderAnimationQ8 = 0;
uint32_t dogAnimationQ8 = 0;
uint32_t nextFrameAt = 0;
uint32_t fpsWindowStart = 0;
uint32_t fpsFrames = 0;
uint32_t buttonDownAt = 0;
uint32_t pendingClickAt = 0;
WeatherState demoWeather;
float demoHour = 12.0F;
bool demoMode = false;
bool singleClickPending = false;
bool longPressHandled = false;
int birdScheduleDay = -1;
int birdScheduleHour = -1;
int lastBirdTriggerKey = -1;
uint8_t birdPassCount = 0;
uint8_t birdPassMinutes[MAX_BIRD_PASSES_PER_HOUR] = {};
bool birdFlockActive = false;
uint32_t birdFlockStartFrame = 0;
uint8_t birdFlockSize = 0;
uint8_t birdFlockColors[MAX_BIRDS_PER_FLOCK] = {};
uint8_t birdFlockKinds[MAX_BIRDS_PER_FLOCK] = {};
int8_t birdFlockXOffsets[MAX_BIRDS_PER_FLOCK] = {};
int8_t birdFlockYOffsets[MAX_BIRDS_PER_FLOCK] = {};
void drawStartupLogo();

const char *weatherText(int code) {
  if (code == 0) return "晴";
  if (code <= 2) return "多云";
  if (code == 3) return "阴";
  if (code == 45 || code == 48) return "雾";
  if (code >= 51 && code <= 57) return "毛毛雨";
  if (code >= 61 && code <= 67) return "雨";
  if (code >= 71 && code <= 77) return "雪";
  if (code >= 80 && code <= 82) return "阵雨";
  if (code >= 85 && code <= 86) return "阵雪";
  if (code >= 95) return "雷雨";
  return "未知";
}

bool isRain(int code) {
  return (code >= 51 && code <= 67) || (code >= 80 && code <= 82) || code >= 95;
}

bool isSnow(int code) {
  return (code >= 71 && code <= 77) || (code >= 85 && code <= 86);
}

bool isThunderstorm(int code) {
  return code >= 95;
}

bool isHail(int code) {
  return code == 96 || code == 99;
}

bool isFreezingRain(int code) {
  return code == 56 || code == 57 || code == 66 || code == 67;
}

bool isRainShower(int code) {
  return code >= 80 && code <= 82;
}

bool isSnowShower(int code) {
  return code == 85 || code == 86;
}

bool dustyAir(const WeatherState &value);
bool sandstormAir(const WeatherState &value);
bool hazeAir(const WeatherState &value);

const uint16_t *scenePixels(SceneKind scene) {
  switch (scene) {
    case SUNSET: return SCENE_SUNSET;
    case NIGHT: return SCENE_NIGHT;
    case RAIN: return SCENE_RAIN;
    case SNOW: return SCENE_SNOW;
    default: return SCENE_DAY;
  }
}

WeatherState weatherSnapshot() {
  WeatherState value;
  portENTER_CRITICAL(&weatherMux);
  value = weather;
  portEXIT_CRITICAL(&weatherMux);
  return value;
}

void publishWeather(const WeatherState &value) {
  portENTER_CRITICAL(&weatherMux);
  weather = value;
  portEXIT_CRITICAL(&weatherMux);
}

int isoTimeMinutes(const char *iso, int fallback) {
  if (iso == nullptr) return fallback;
  const char *separator = strchr(iso, 'T');
  if (separator == nullptr || strlen(separator) < 6) return fallback;
  int hour = (separator[1] - '0') * 10 + (separator[2] - '0');
  int minute = (separator[4] - '0') * 10 + (separator[5] - '0');
  if (hour < 0 || hour > 23 || minute < 0 || minute > 59) return fallback;
  return hour * 60 + minute;
}

bool enrichAirQualityNow(WeatherState &value) {
  if (WiFi.status() != WL_CONNECTED) return false;

  char url[360];
  snprintf(url, sizeof(url),
           "https://air-quality-api.open-meteo.com/v1/air-quality?latitude=%.4f&longitude=%.4f"
           "&current=pm10,dust,aerosol_optical_depth"
           "&forecast_days=1&timezone=Asia%%2FTokyo",
           LATITUDE, LONGITUDE);

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(8000);
  if (!http.begin(client, url)) return false;
  int status = http.GET();
  if (status != HTTP_CODE_OK) {
    Serial.printf("空气质量请求失败：HTTP %d\n", status);
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload);
  if (error) {
    Serial.printf("空气质量解析失败：%s\n", error.c_str());
    return false;
  }

  value.pm10 = doc["current"]["pm10"].as<float>();
  value.dust = doc["current"]["dust"].as<float>();
  value.aerosolOpticalDepth = doc["current"]["aerosol_optical_depth"].as<float>();
  return true;
}

bool fetchWeatherNow() {
  if (WiFi.status() != WL_CONNECTED) return false;

  char url[720];
  snprintf(url, sizeof(url),
           "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
           "&current=temperature_2m,apparent_temperature,weather_code,"
           "precipitation,rain,snowfall,cloud_cover,wind_speed_10m,wind_gusts_10m,visibility"
           "&hourly=weather_code,precipitation_probability"
           "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,sunrise,sunset"
           "&forecast_days=7&temperature_unit=celsius&wind_speed_unit=ms"
           "&precipitation_unit=mm&timezone=Asia%%2FTokyo",
           LATITUDE, LONGITUDE);

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(12000);
  if (!http.begin(client, url)) return false;
  int status = http.GET();
  if (status != HTTP_CODE_OK) {
    Serial.printf("天气请求失败：HTTP %d\n", status);
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload);
  if (error) {
    Serial.printf("天气解析失败：%s\n", error.c_str());
    return false;
  }

  WeatherState value;
  value.temperature = doc["current"]["temperature_2m"].as<float>();
  value.apparentTemperature = doc["current"]["apparent_temperature"].as<float>();
  value.code = doc["current"]["weather_code"].as<int>();
  struct tm now{};
  int weatherLookaheadHour = getLocalTime(&now, 20) ? min(23, now.tm_hour + 2) : 2;
  value.nextCode = doc["hourly"]["weather_code"][weatherLookaheadHour] | value.code;
  value.precipitation = doc["current"]["precipitation"].as<float>();
  value.rain = doc["current"]["rain"].as<float>();
  value.snowfall = doc["current"]["snowfall"].as<float>();
  value.cloudCover = doc["current"]["cloud_cover"].as<float>();
  value.windSpeed = doc["current"]["wind_speed_10m"].as<float>();
  value.windGust = doc["current"]["wind_gusts_10m"].as<float>();
  value.visibility = doc["current"]["visibility"].as<float>();
  value.dailyMax = doc["daily"]["temperature_2m_max"][0].as<float>();
  value.dailyMin = doc["daily"]["temperature_2m_min"][0].as<float>();
  value.sunriseMinutes = isoTimeMinutes(doc["daily"]["sunrise"][0].as<const char *>(), 360);
  value.sunsetMinutes = isoTimeMinutes(doc["daily"]["sunset"][0].as<const char *>(), 1080);
  JsonArray dailyCodes = doc["daily"]["weather_code"].as<JsonArray>();
  JsonArray dailyMax = doc["daily"]["temperature_2m_max"].as<JsonArray>();
  JsonArray dailyMin = doc["daily"]["temperature_2m_min"].as<JsonArray>();
  JsonArray dailyPrecipitation = doc["daily"]["precipitation_probability_max"].as<JsonArray>();
  size_t forecastCount = min(static_cast<size_t>(FORECAST_DAYS), dailyMax.size());
  value.forecastCount = static_cast<uint8_t>(forecastCount);
  for (uint8_t i = 0; i < value.forecastCount; ++i) {
    value.forecastCode[i] = dailyCodes[i] | value.code;
    value.forecastMax[i] = dailyMax[i].as<float>();
    value.forecastMin[i] = dailyMin[i].as<float>();
    value.forecastPrecipitation[i] =
        static_cast<uint8_t>(constrain(dailyPrecipitation[i].as<int>(), 0, 100));
  }
  JsonArray probabilities = doc["hourly"]["precipitation_probability"].as<JsonArray>();
  size_t probabilityCount = min(static_cast<size_t>(PRECIPITATION_HOURS), probabilities.size());
  value.precipitationProbabilityCount = static_cast<uint8_t>(probabilityCount);
  for (uint8_t i = 0; i < value.precipitationProbabilityCount; ++i) {
    value.precipitationProbability[i] =
        static_cast<uint8_t>(constrain(probabilities[i].as<int>(), 0, 100));
  }
  enrichAirQualityNow(value);
  value.valid = true;
  publishWeather(value);
  lastWeatherFetch = millis();
  Serial.printf("朝来市：%.1f°C，%s，风 %.1f/阵风 %.1fm/s，日出 %02d:%02d，日落 %02d:%02d\n",
                value.temperature, weatherText(value.code), value.windSpeed, value.windGust,
                value.sunriseMinutes / 60, value.sunriseMinutes % 60,
                value.sunsetMinutes / 60, value.sunsetMinutes % 60);
  return true;
}

bool updateHolidayForToday() {
  if (WiFi.status() != WL_CONNECTED) return false;

  struct tm now{};
  if (!getLocalTime(&now, 20)) return false;
  char today[11];
  snprintf(today, sizeof(today), "%04d-%02d-%02d",
           now.tm_year + 1900, now.tm_mon + 1, now.tm_mday);

  static char checkedDate[11] = "";
  if (strcmp(today, checkedDate) == 0) return true;

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(12000);
  if (!http.begin(client, "https://holidays-jp.github.io/api/v1/date.json")) {
    return false;
  }
  int status = http.GET();
  if (status != HTTP_CODE_OK) {
    Serial.printf("祝日请求失败：HTTP %d\n", status);
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();
  String key = String('"') + today + "\"";
  holidayToday = payload.indexOf(key) >= 0;
  strlcpy(checkedDate, today, sizeof(checkedDate));
  Serial.printf("日本祝日：%s %s\n", today, holidayToday ? "是" : "否");
  return true;
}

void weatherTask(void *) {
  uint32_t retryAt = 0;
  uint32_t holidayRetryAt = 0;
  uint32_t reconnectAt = 0;
  for (;;) {
    uint32_t now = millis();
    if (WiFi.status() != WL_CONNECTED && knownNetworkCount > 0 && now >= reconnectAt) {
      if (knownNetworks.run(4000) == WL_CONNECTED) {
        Serial.printf("Wi-Fi 已切换至：%s\n", WiFi.SSID().c_str());
        requestWeather = true;
      }
      reconnectAt = millis() + 10000;
    }
    bool due = requestWeather ||
               (lastWeatherFetch != 0 && now - lastWeatherFetch >= WEATHER_REFRESH_MS) ||
               (lastWeatherFetch == 0 && now >= retryAt);
    if (due && WiFi.status() == WL_CONNECTED) {
      requestWeather = false;
      if (!fetchWeatherNow()) retryAt = millis() + 30000;
    }
    if (now >= holidayRetryAt && !updateHolidayForToday()) {
      holidayRetryAt = millis() + 30000;
    }
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

void drawChinese(const char *text, int x, int baseline, uint16_t color) {
  zh.setForegroundColor(color);
  zh.setCursor(x, baseline);
  zh.print(text);
}

void drawStartupLogo() {
  display.drawRGBBitmap(0, 0, STARTUP_LOGO_BITMAP, STARTUP_LOGO_WIDTH, STARTUP_LOGO_HEIGHT);
}

void applyAdminSettings() {
  uint8_t duty = static_cast<uint8_t>(map(adminSettings.brightness, 0, 100, 0, 255));
  analogWrite(TFT_BACKLITE, duty);
}

void loadAdminSettings() {
  Preferences preferences;
  if (!preferences.begin("clockui", true)) return;
  adminSettings.outfitMode = preferences.getUChar("outfitMode", adminSettings.outfitMode);
  adminSettings.manualOutfit = preferences.getUChar("manualOutfit", adminSettings.manualOutfit);
  adminSettings.brightness = preferences.getUChar("brightness", adminSettings.brightness);
  adminSettings.animationSpeed = preferences.getUChar("animSpeed", adminSettings.animationSpeed);
  adminSettings.riderSpeed = preferences.getUChar("riderSpeed", adminSettings.riderSpeed);
  adminSettings.backgroundSpeed = preferences.getUChar("bgSpeed", adminSettings.backgroundSpeed);
  adminSettings.dogSpeed = preferences.getUChar("dogSpeed", adminSettings.dogSpeed);
  adminSettings.dogTint = preferences.getUChar("dogTint", adminSettings.dogTint);
  adminSettings.dogNightBright = preferences.getUChar("dogNight", adminSettings.dogNightBright);
  adminSettings.theme = preferences.getUChar("theme", adminSettings.theme);
  adminSettings.showWifi = preferences.getBool("showWifi", adminSettings.showWifi);
  adminSettings.showSunSchedule = preferences.getBool("showSun", adminSettings.showSunSchedule);
  adminSettings.showPrecipitation = preferences.getBool("showPrecip", adminSettings.showPrecipitation);
  adminSettings.showFeelsLike = preferences.getBool("showFeels", adminSettings.showFeelsLike);
  adminSettings.showMoon = preferences.getBool("showMoon", adminSettings.showMoon);
  adminSettings.showHoliday = preferences.getBool("showHoliday", adminSettings.showHoliday);
  adminSettings.showTemperature = preferences.getBool("showTemp", adminSettings.showTemperature);
  adminSettings.showDog = preferences.getBool("showDog", adminSettings.showDog);
  adminSettings.showBirds = preferences.getBool("showBirds", adminSettings.showBirds);
  adminSettings.showWeatherEffects = preferences.getBool("showEffects", adminSettings.showWeatherEffects);
  preferences.end();
  adminSettings.manualOutfit = min<uint8_t>(adminSettings.manualOutfit, 5);
  adminSettings.brightness = constrain(adminSettings.brightness, 20, 100);
  adminSettings.animationSpeed = constrain(adminSettings.animationSpeed, 60, 140);
  adminSettings.riderSpeed = constrain(adminSettings.riderSpeed, 60, 140);
  adminSettings.backgroundSpeed = constrain(adminSettings.backgroundSpeed, 60, 150);
  adminSettings.dogSpeed = constrain(adminSettings.dogSpeed, 60, 160);
  adminSettings.dogTint = min<uint8_t>(adminSettings.dogTint, 4);
  adminSettings.dogNightBright = constrain(adminSettings.dogNightBright, 40, 100);
}

void saveAdminSettings() {
  Preferences preferences;
  if (!preferences.begin("clockui", false)) return;
  preferences.putUChar("outfitMode", adminSettings.outfitMode);
  preferences.putUChar("manualOutfit", adminSettings.manualOutfit);
  preferences.putUChar("brightness", adminSettings.brightness);
  preferences.putUChar("animSpeed", adminSettings.animationSpeed);
  preferences.putUChar("riderSpeed", adminSettings.riderSpeed);
  preferences.putUChar("bgSpeed", adminSettings.backgroundSpeed);
  preferences.putUChar("dogSpeed", adminSettings.dogSpeed);
  preferences.putUChar("dogTint", adminSettings.dogTint);
  preferences.putUChar("dogNight", adminSettings.dogNightBright);
  preferences.putUChar("theme", adminSettings.theme);
  preferences.putBool("showWifi", adminSettings.showWifi);
  preferences.putBool("showSun", adminSettings.showSunSchedule);
  preferences.putBool("showPrecip", adminSettings.showPrecipitation);
  preferences.putBool("showFeels", adminSettings.showFeelsLike);
  preferences.putBool("showMoon", adminSettings.showMoon);
  preferences.putBool("showHoliday", adminSettings.showHoliday);
  preferences.putBool("showTemp", adminSettings.showTemperature);
  preferences.putBool("showDog", adminSettings.showDog);
  preferences.putBool("showBirds", adminSettings.showBirds);
  preferences.putBool("showEffects", adminSettings.showWeatherEffects);
  preferences.end();
}

uint16_t scaledAnimationStep(uint16_t base, uint8_t percent) {
  uint32_t combined = static_cast<uint32_t>(adminSettings.animationSpeed) * percent / 100;
  return max<uint16_t>(1, static_cast<uint32_t>(base) * combined / 100);
}

void drawStatus(const char *title, const char *message, uint16_t color) {
  display.fillScreen(0x0862);
  display.fillRoundRect(8, 25, 224, 82, 8, 0x10E4);
  U8G2_FOR_ADAFRUIT_GFX directText;
  directText.begin(display);
  directText.setFont(u8g2_font_wqy12_t_gb2312);
  directText.setFontMode(1);
  directText.setForegroundColor(color);
  directText.setCursor(20, 53);
  directText.print(title);
  directText.setForegroundColor(WHITE);
  directText.setCursor(20, 82);
  directText.print(message);
}

void onConfigPortal(WiFiManager *) {
  display.fillScreen(0x0862);
  display.fillRoundRect(7, 7, 226, 121, 8, 0x10E4);
  U8G2_FOR_ADAFRUIT_GFX directText;
  directText.begin(display);
  directText.setFont(u8g2_font_wqy12_t_gb2312);
  directText.setFontMode(1);
  directText.setForegroundColor(CYAN);
  directText.setCursor(17, 28);
  directText.print("旅行天气时钟 V2");
  directText.setForegroundColor(WHITE);
  directText.setCursor(17, 51);
  directText.print("手机连接热点：");
  display.setTextColor(WARM);
  display.setTextSize(2);
  display.setCursor(17, 61);
  display.print("TravelClock-Setup");
  directText.setForegroundColor(MUTED);
  directText.setCursor(17, 99);
  directText.print("浏览器打开 192.168.4.1");
  directText.setCursor(17, 119);
  directText.print("选择新的 Wi-Fi 并保存");
}

String wifiPreferenceKey(const char *prefix, uint8_t index) {
  char key[12];
  snprintf(key, sizeof(key), "%s%u", prefix, index);
  return String(key);
}

void loadKnownNetworks() {
  knownNetworks.APlistClean();
  knownNetworkCount = 0;
  Preferences preferences;
  if (!preferences.begin("travelwifi", true)) return;
  uint8_t count = min(preferences.getUChar("count", 0), MAX_KNOWN_WIFI_NETWORKS);
  for (uint8_t index = 0; index < count; ++index) {
    String ssid = preferences.getString(wifiPreferenceKey("ssid", index).c_str(), "");
    String password = preferences.getString(wifiPreferenceKey("pass", index).c_str(), "");
    if (ssid.isEmpty()) continue;
    knownNetworks.addAP(ssid.c_str(), password.c_str());
    ++knownNetworkCount;
  }
  preferences.end();
  Serial.printf("已载入 %u 个 Wi-Fi 网络\n", knownNetworkCount);
}

void rememberNetwork(const String &ssid, const String &password) {
  if (ssid.isEmpty()) return;
  Preferences preferences;
  if (!preferences.begin("travelwifi", false)) return;
  uint8_t count = min(preferences.getUChar("count", 0), MAX_KNOWN_WIFI_NETWORKS);
  int destination = -1;
  for (uint8_t index = 0; index < count; ++index) {
    if (preferences.getString(wifiPreferenceKey("ssid", index).c_str(), "") == ssid) {
      destination = index;
      break;
    }
  }
  if (destination < 0 && count < MAX_KNOWN_WIFI_NETWORKS) {
    destination = count++;
    preferences.putUChar("count", count);
  } else if (destination < 0) {
    destination = preferences.getUChar("next", 0) % MAX_KNOWN_WIFI_NETWORKS;
    preferences.putUChar("next", (destination + 1) % MAX_KNOWN_WIFI_NETWORKS);
  }
  preferences.putString(wifiPreferenceKey("ssid", destination).c_str(), ssid);
  preferences.putString(wifiPreferenceKey("pass", destination).c_str(), password);
  preferences.end();
  Serial.printf("已记住 Wi-Fi：%s\n", ssid.c_str());
  loadKnownNetworks();
}

void clearKnownNetworks() {
  Preferences preferences;
  if (preferences.begin("travelwifi", false)) {
    preferences.clear();
    preferences.end();
  }
  knownNetworks.APlistClean();
  knownNetworkCount = 0;
}

bool connectKnownNetwork(uint32_t timeoutMs) {
  if (knownNetworkCount == 0) return false;
  WiFi.mode(WIFI_STA);
  uint32_t startedAt = millis();
  while (millis() - startedAt < timeoutMs) {
    if (knownNetworks.run(500) == WL_CONNECTED) {
      Serial.printf("自动连接 Wi-Fi：%s (%d dBm)\n", WiFi.SSID().c_str(), WiFi.RSSI());
      return true;
    }
    delay(20);
  }
  return false;
}

uint16_t blend565(uint16_t a, uint16_t b, uint8_t amount) {
  uint16_t inv = 255 - amount;
  uint16_t r = ((((a >> 11) & 31) * inv) + (((b >> 11) & 31) * amount)) / 255;
  uint16_t g = ((((a >> 5) & 63) * inv) + (((b >> 5) & 63) * amount)) / 255;
  uint16_t bl = (((a & 31) * inv) + ((b & 31) * amount)) / 255;
  return (r << 11) | (g << 5) | bl;
}

uint16_t grade565(uint16_t color, uint16_t redScale, uint16_t greenScale,
                  uint16_t blueScale, uint8_t desaturate) {
  int r = ((color >> 11) & 31) * 255 / 31;
  int g = ((color >> 5) & 63) * 255 / 63;
  int b = (color & 31) * 255 / 31;
  r = min(255, r * redScale / 255);
  g = min(255, g * greenScale / 255);
  b = min(255, b * blueScale / 255);
  if (desaturate > 0) {
    int gray = (r * 54 + g * 183 + b * 19) >> 8;
    r = (r * (255 - desaturate) + gray * desaturate) / 255;
    g = (g * (255 - desaturate) + gray * desaturate) / 255;
    b = (b * (255 - desaturate) + gray * desaturate) / 255;
  }
  return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}

void paintBackground(const WeatherState &value, float hour, bool moving) {
  uint16_t *buffer = frameBuffer.getBuffer();
  const uint16_t *to = scenePixels(currentScene);
  const uint16_t *from = scenePixels(previousScene);
  uint16_t scrollX = (backgroundScrollQ8 >> 8) % SCENE_TILE_WIDTH;
  uint8_t amount = crossfadeFrame >= CROSSFADE_FRAMES
                       ? 255
                       : static_cast<uint8_t>((crossfadeFrame * 255UL) / CROSSFADE_FRAMES);
  uint8_t light = 255;
  uint16_t redScale = 255;
  uint16_t greenScale = 255;
  uint16_t blueScale = 255;
  uint8_t desaturate = 0;
  if (currentScene != NIGHT && currentScene != RAIN) {
    float minute = hour * 60.0F;
    float daylight = constrain((minute - value.sunriseMinutes) /
                                   max(1.0F, static_cast<float>(value.sunsetMinutes - value.sunriseMinutes)),
                               0.0F, 1.0F);
    float elevation = sinf(daylight * PI);
    float warmth = constrain((1.0F - elevation) * 1.15F, 0.0F, 1.0F);
    float cloudShade = constrain(value.cloudCover, 0.0F, 100.0F) * 0.42F;
    float rainShade = value.precipitation > 0.0F ? min(25.0F, value.precipitation * 8.0F) : 0.0F;
    light = static_cast<uint8_t>(constrain(145.0F + elevation * 110.0F - cloudShade - rainShade,
                                           105.0F, 255.0F));
    redScale = static_cast<uint16_t>(constrain(light * (1.0F + warmth * 0.12F), 0.0F, 285.0F));
    greenScale = static_cast<uint16_t>(light * (1.0F - warmth * 0.06F));
    blueScale = static_cast<uint16_t>(light * (1.0F - warmth * 0.27F));
    desaturate = static_cast<uint8_t>(constrain(value.cloudCover * 0.42F, 0.0F, 45.0F));
  }

  for (int y = 0; y < SCENE_HEIGHT; ++y) {
    int sourceRow = y * SCENE_TILE_WIDTH;
    int targetRow = y * SCENE_WIDTH;
    for (int x = 0; x < SCENE_WIDTH; ++x) {
      int sourceX = scrollX + x;
      if (sourceX >= SCENE_TILE_WIDTH) sourceX -= SCENE_TILE_WIDTH;
      int sourceIndex = sourceRow + sourceX;
      uint16_t b = pgm_read_word(&to[sourceIndex]);
      uint16_t pixel = amount == 255
                           ? b
                           : blend565(pgm_read_word(&from[sourceIndex]), b, amount);
      buffer[targetRow + x] =
          (redScale == 255 && greenScale == 255 && blueScale == 255 && desaturate == 0)
              ? pixel
              : grade565(pixel, redScale, greenScale, blueScale, desaturate);
    }
  }
  if (crossfadeFrame < CROSSFADE_FRAMES) ++crossfadeFrame;
  // Ground motion is independent from the clock-driven sun and moon, and is
  // frozen while the traveler is resting at camp.
  if (moving) backgroundScrollQ8 += scaledAnimationStep(BACKGROUND_STEP_Q8, adminSettings.backgroundSpeed);
}

void blendFramePixel(int x, int y, uint16_t color, uint8_t amount) {
  if (x < 0 || x >= SCENE_WIDTH || y < 22 || y >= 113) return;
  uint16_t *buffer = frameBuffer.getBuffer();
  int index = y * SCENE_WIDTH + x;
  buffer[index] = blend565(buffer[index], color, amount);
}

float currentHour() {
  struct tm now{};
  if (!getLocalTime(&now, 5)) return 12.0F;
  return now.tm_hour + now.tm_min / 60.0F + now.tm_sec / 3600.0F;
}

float moonPhaseFromEpoch(time_t epoch) {
  constexpr double NEW_MOON_EPOCH = 947182440.0;  // 2000-01-06 18:14 UTC.
  constexpr double SYNODIC_MONTH_SECONDS = 29.530588853 * 86400.0;
  double elapsed = difftime(epoch, static_cast<time_t>(NEW_MOON_EPOCH));
  double phase = fmod(elapsed, SYNODIC_MONTH_SECONDS) / SYNODIC_MONTH_SECONDS;
  if (phase < 0.0) phase += 1.0;
  return static_cast<float>(phase);
}

struct EquatorialPosition {
  double ra = 0.0;
  double dec = 0.0;
};

struct LocalSkyVector {
  double east = 0.0;
  double north = 0.0;
  double up = 0.0;
};

double normalizeDegrees(double degrees) {
  degrees = fmod(degrees, 360.0);
  if (degrees < 0.0) degrees += 360.0;
  return degrees;
}

double julianDateFromEpoch(time_t epoch) {
  return static_cast<double>(epoch) / 86400.0 + 2440587.5;
}

EquatorialPosition sunEquatorial(time_t epoch) {
  constexpr double DEG = 0.017453292519943295;
  double d = julianDateFromEpoch(epoch) - 2451545.0;
  double meanAnomaly = normalizeDegrees(357.529 + 0.98560028 * d) * DEG;
  double meanLongitude = normalizeDegrees(280.459 + 0.98564736 * d);
  double eclipticLongitude =
      normalizeDegrees(meanLongitude + 1.915 * sin(meanAnomaly) + 0.020 * sin(2.0 * meanAnomaly)) * DEG;
  double obliquity = (23.439 - 0.00000036 * d) * DEG;

  EquatorialPosition result;
  result.ra = atan2(cos(obliquity) * sin(eclipticLongitude), cos(eclipticLongitude));
  if (result.ra < 0.0) result.ra += TWO_PI;
  result.dec = asin(sin(obliquity) * sin(eclipticLongitude));
  return result;
}

EquatorialPosition moonEquatorial(time_t epoch) {
  constexpr double DEG = 0.017453292519943295;
  double d = julianDateFromEpoch(epoch) - 2451543.5;

  double node = normalizeDegrees(125.1228 - 0.0529538083 * d) * DEG;
  double inclination = 5.1454 * DEG;
  double perigee = normalizeDegrees(318.0634 + 0.1643573223 * d) * DEG;
  double eccentricity = 0.054900;
  double meanAnomaly = normalizeDegrees(115.3654 + 13.0649929509 * d) * DEG;
  double eccentricAnomaly =
      meanAnomaly + eccentricity * sin(meanAnomaly) * (1.0 + eccentricity * cos(meanAnomaly));
  double xv = cos(eccentricAnomaly) - eccentricity;
  double yv = sqrt(1.0 - eccentricity * eccentricity) * sin(eccentricAnomaly);
  double trueAnomaly = atan2(yv, xv);
  double radius = sqrt(xv * xv + yv * yv);
  double argument = trueAnomaly + perigee;

  double xecliptic = radius * (cos(node) * cos(argument) - sin(node) * sin(argument) * cos(inclination));
  double yecliptic = radius * (sin(node) * cos(argument) + cos(node) * sin(argument) * cos(inclination));
  double zecliptic = radius * (sin(argument) * sin(inclination));
  double eclipticLongitude = atan2(yecliptic, xecliptic);
  double eclipticLatitude = atan2(zecliptic, sqrt(xecliptic * xecliptic + yecliptic * yecliptic));
  double obliquity = (23.4393 - 0.00000036 * (julianDateFromEpoch(epoch) - 2451545.0)) * DEG;

  double x = cos(eclipticLongitude) * cos(eclipticLatitude);
  double y = sin(eclipticLongitude) * cos(eclipticLatitude) * cos(obliquity) -
             sin(eclipticLatitude) * sin(obliquity);
  double z = sin(eclipticLongitude) * cos(eclipticLatitude) * sin(obliquity) +
             sin(eclipticLatitude) * cos(obliquity);

  EquatorialPosition result;
  result.ra = atan2(y, x);
  if (result.ra < 0.0) result.ra += TWO_PI;
  result.dec = asin(z);
  return result;
}

LocalSkyVector equatorialToLocal(const EquatorialPosition &position, time_t epoch) {
  constexpr double DEG = 0.017453292519943295;
  double jd = julianDateFromEpoch(epoch);
  double d = jd - 2451545.0;
  double gmst = normalizeDegrees(280.46061837 + 360.98564736629 * d);
  double lst = normalizeDegrees(gmst + LONGITUDE) * DEG;
  double hourAngle = lst - position.ra;
  double latitude = LATITUDE * DEG;

  LocalSkyVector result;
  result.east = -cos(position.dec) * sin(hourAngle);
  result.north = sin(position.dec) * cos(latitude) -
                 cos(position.dec) * cos(hourAngle) * sin(latitude);
  result.up = sin(position.dec) * sin(latitude) +
              cos(position.dec) * cos(hourAngle) * cos(latitude);
  return result;
}

double vectorDot(const LocalSkyVector &a, const LocalSkyVector &b) {
  return a.east * b.east + a.north * b.north + a.up * b.up;
}

LocalSkyVector vectorSubtractScaled(const LocalSkyVector &a, const LocalSkyVector &b, double scale) {
  return {a.east - b.east * scale, a.north - b.north * scale, a.up - b.up * scale};
}

LocalSkyVector vectorCross(const LocalSkyVector &a, const LocalSkyVector &b) {
  return {a.north * b.up - a.up * b.north,
          a.up * b.east - a.east * b.up,
          a.east * b.north - a.north * b.east};
}

LocalSkyVector vectorNormalize(LocalSkyVector value) {
  double length = sqrt(vectorDot(value, value));
  if (length < 0.000001) return {1.0, 0.0, 0.0};
  return {value.east / length, value.north / length, value.up / length};
}

float moonBrightLimbAngle(time_t epoch) {
  LocalSkyVector sun = vectorNormalize(equatorialToLocal(sunEquatorial(epoch), epoch));
  LocalSkyVector moon = vectorNormalize(equatorialToLocal(moonEquatorial(epoch), epoch));
  LocalSkyVector sunOnMoonDisk = vectorNormalize(vectorSubtractScaled(sun, moon, vectorDot(sun, moon)));

  LocalSkyVector localZenith{0.0, 0.0, 1.0};
  LocalSkyVector diskUp = vectorNormalize(vectorSubtractScaled(localZenith, moon, vectorDot(localZenith, moon)));
  LocalSkyVector diskRight = vectorNormalize(vectorCross(moon, diskUp));

  double x = vectorDot(sunOnMoonDisk, diskRight);
  double y = vectorDot(sunOnMoonDisk, diskUp);
  return static_cast<float>(atan2(y, x));
}

SceneKind automaticScene(const WeatherState &value, float hour) {
  float minute = hour * 60.0F;
  if (minute < value.sunriseMinutes || minute >= value.sunsetMinutes) return NIGHT;
  if (minute < value.sunriseMinutes + 75 || minute >= value.sunsetMinutes - 75) return SUNSET;
  return DAY;
}

SceneKind desiredScene(const WeatherState &value, float hour) {
  if (previewMode == 0) return automaticScene(value, hour);
  return static_cast<SceneKind>(previewMode - 1);
}

void updateSceneTarget(SceneKind desired) {
  if (desired == currentScene) return;
  previousScene = currentScene;
  currentScene = desired;
  crossfadeFrame = 0;
}

void drawSunHalo(int cx, int cy, float cloudCover, float elevation) {
  int outerRadius = cloudCover < 25.0F ? 22 : 16;
  uint8_t strength = static_cast<uint8_t>(constrain(50.0F - cloudCover * 0.28F,
                                                     18.0F, 50.0F));
  for (int y = -outerRadius; y <= outerRadius; ++y) {
    for (int x = -outerRadius; x <= outerRadius; ++x) {
      int distance2 = x * x + y * y;
      if (distance2 > outerRadius * outerRadius || distance2 < 36) continue;
      float falloff = 1.0F - sqrtf(distance2) / outerRadius;
      uint8_t amount = static_cast<uint8_t>(strength * falloff);
      blendFramePixel(cx + x, cy + y, elevation < 0.35F ? 0xFD28 : 0xFFE9, amount);
    }
  }
  if (cloudCover < 18.0F) {
    uint16_t rayColor = elevation < 0.35F ? 0xFD28 : 0xFF69;
    frameBuffer.drawLine(cx - 13, cy, cx - 10, cy, rayColor);
    frameBuffer.drawLine(cx + 10, cy, cx + 13, cy, rayColor);
    frameBuffer.drawLine(cx, cy - 13, cx, cy - 10, rayColor);
    frameBuffer.drawLine(cx - 9, cy - 9, cx - 7, cy - 7, rayColor);
    frameBuffer.drawLine(cx + 7, cy - 7, cx + 9, cy - 9, rayColor);
  }
}

void drawSkySunIcon(int cx, int cy, float cloudCover, float elevation) {
  drawSunHalo(cx, cy, cloudCover, elevation);
  uint16_t rayColor = cloudCover > 70.0F ? 0xDDF7 : (elevation < 0.35F ? 0xFDA7 : 0xFEC8);
  uint16_t faceColor = cloudCover > 70.0F ? 0xEEDB : 0xFE84;
  uint16_t rimColor = elevation < 0.35F ? 0xFD23 : 0xFDE0;
  constexpr int rayInner = 7;
  constexpr int rayOuter = 10;
  constexpr int rayHalf = 2;
  frameBuffer.fillTriangle(cx, cy - rayOuter, cx - rayHalf, cy - rayInner, cx + rayHalf, cy - rayInner, rayColor);
  frameBuffer.fillTriangle(cx, cy + rayOuter, cx - rayHalf, cy + rayInner, cx + rayHalf, cy + rayInner, rayColor);
  frameBuffer.fillTriangle(cx - rayOuter, cy, cx - rayInner, cy - rayHalf, cx - rayInner, cy + rayHalf, rayColor);
  frameBuffer.fillTriangle(cx + rayOuter, cy, cx + rayInner, cy - rayHalf, cx + rayInner, cy + rayHalf, rayColor);
  frameBuffer.fillTriangle(cx - 8, cy - 8, cx - 6, cy - 3, cx - 3, cy - 6, rayColor);
  frameBuffer.fillTriangle(cx + 8, cy - 8, cx + 6, cy - 3, cx + 3, cy - 6, rayColor);
  frameBuffer.fillTriangle(cx - 8, cy + 8, cx - 6, cy + 3, cx - 3, cy + 6, rayColor);
  frameBuffer.fillTriangle(cx + 8, cy + 8, cx + 6, cy + 3, cx + 3, cy + 6, rayColor);
  frameBuffer.fillCircle(cx, cy, 6, faceColor);
  frameBuffer.drawCircle(cx, cy, 6, rimColor);
  frameBuffer.drawCircle(cx, cy, 7, blend565(rimColor, WHITE, 70));
  frameBuffer.drawPixel(cx - 3, cy - 4, 0xFFFF);
}

bool moonLitPixel(float phase, int px, int py, int radius, float brightLimbAngle) {
  if (phase < 0.0F) return false;
  float x = static_cast<float>(px) / radius;
  float y = -static_cast<float>(py) / radius;
  float disk = x * x + y * y;
  if (disk > 1.0F) return false;
  float z = sqrtf(max(0.0F, 1.0F - disk));
  float axisX = x * cosf(brightLimbAngle) + y * sinf(brightLimbAngle);
  float angle = phase * TWO_PI;
  float sunX = sinf(angle);
  float sunZ = -cosf(angle);
  return axisX * sunX + z * sunZ > 0.0F;
}

void drawMoonSilhouette(int cx, int cy, int radius, float phase, uint16_t color,
                        bool glow, bool drawShadow, float brightLimbAngle) {
  bool newMoonRing = phase >= 0.0F && (phase < 0.035F || phase > 0.965F);
  if (newMoonRing) {
    frameBuffer.drawCircle(cx, cy, radius, blend565(color, 0xFEC8, 80));
    if (glow) frameBuffer.drawCircle(cx, cy, radius + 1, 0xA4A1);
    return;
  }
  for (int py = -radius; py <= radius; ++py) {
    for (int px = -radius; px <= radius; ++px) {
      float normalizedX = static_cast<float>(px) / radius;
      float normalizedY = static_cast<float>(py) / radius;
      float disk = normalizedX * normalizedX + normalizedY * normalizedY;
      if (disk > 1.0F) continue;
      bool lit = moonLitPixel(phase, px, py, radius, brightLimbAngle);
      if (!lit && !drawShadow) continue;
      uint16_t pixelColor = lit ? color : (glow ? 0x18C5 : 0x2945);
      if (!lit) {
        float edgeFade = constrain((1.0F - disk) * 1.7F, 0.0F, 1.0F);
        uint8_t shadowAlpha = static_cast<uint8_t>((glow ? 94 : 58) * edgeFade);
        blendFramePixel(cx + px, cy + py, pixelColor, shadowAlpha);
        continue;
      }
      if (glow && (px * px + py * py > (radius - 2) * (radius - 2))) {
        pixelColor = blend565(color, 0xFEC8, 42);
      }
      frameBuffer.drawPixel(cx + px, cy + py, pixelColor);
    }
  }
  if (glow && phase > 0.42F && phase < 0.58F) frameBuffer.drawCircle(cx, cy, radius, 0xF6CB);
}

void drawSkyMoonIcon(int cx, int cy, float phase, float brightLimbAngle) {
  constexpr int radius = 7;
  for (int py = -13; py <= 13; ++py) {
    for (int px = -13; px <= 13; ++px) {
      int distance2 = px * px + py * py;
      if (distance2 <= radius * radius || distance2 > 13 * 13) continue;
      float falloff = 1.0F - sqrtf(static_cast<float>(distance2)) / 13.0F;
      uint8_t amount = static_cast<uint8_t>(constrain(falloff * 34.0F, 0.0F, 22.0F));
      if (amount > 0) blendFramePixel(cx + px, cy + py, 0xDFFF, amount);
    }
  }
  drawMoonSilhouette(cx, cy, radius, phase, 0xFFDE, true, true, brightLimbAngle);
  frameBuffer.drawPixel(cx - 3, cy - 5, 0xFFFF);
}

void drawCelestialBody(const WeatherState &value, float hour) {
  bool wetSky = isRain(value.code) || value.rain > 0.05F || value.precipitation > 1.0F;
  if (currentScene == DAY || currentScene == SUNSET) {
    if (wetSky || value.cloudCover >= 94.0F) return;
    float minute = hour * 60.0F;
    float phase = previewMode != 0
                      ? 0.55F
                      : constrain((minute - value.sunriseMinutes) /
                                      max(1.0F, static_cast<float>(value.sunsetMinutes - value.sunriseMinutes)),
                                  0.0F, 1.0F);
    int x = 10 + static_cast<int>(phase * 220);
    float centered = phase * 2.0F - 1.0F;
    float elevation = 1.0F - centered * centered;
  int y = max(33, 65 - static_cast<int>(elevation * 42.0F));
    drawSkySunIcon(x, y, value.cloudCover, elevation);
  } else if (currentScene == NIGHT) {
    if (value.cloudCover < 88.0F) {
      for (int i = 0; i < 9; ++i) {
        int sx = (i * 29 + 17) % 230 + 5;
        int sy = (i * 13 + 9) % 58 + 23;
        uint16_t c = ((animationFrame / 6 + i) % 3 == 0) ? WHITE : 0x6D7F;
        frameBuffer.drawPixel(sx, sy, c);
      }
    }
    if (wetSky || value.cloudCover >= 82.0F) return;

    struct tm now{};
    bool timeReady = getLocalTime(&now, 5);
    float moonPhase = timeReady ? moonPhaseFromEpoch(time(nullptr)) : -1.0F;
    if (moonPhase < 0.0F) return;
    float brightLimbAngle = moonBrightLimbAngle(time(nullptr));
    float solarNoon = (value.sunriseMinutes + value.sunsetMinutes) / 120.0F;
    float moonTransit = fmodf(solarNoon + moonPhase * 24.0F, 24.0F);
    float hourDelta = hour - moonTransit;
    if (hourDelta > 12.0F) hourDelta -= 24.0F;
    if (hourDelta < -12.0F) hourDelta += 24.0F;
    if (fabsf(hourDelta) > 6.0F) return;

    int x = 120 + static_cast<int>(hourDelta * 18.0F);
    float centered = hourDelta / 6.0F;
    int y = 65 - static_cast<int>((1.0F - centered * centered) * 37.0F);
    drawSkyMoonIcon(x, y, moonPhase, brightLimbAngle);
  }
}

enum OutfitKind : uint8_t { SHORT_SLEEVE, SHIRT, SWEATER, JACKET, COAT, RAINCOAT };

OutfitKind recommendedOutfit(const WeatherState &value) {
  if (isRain(value.code) || value.rain > 0.05F) return RAINCOAT;
  float feels = isnan(value.apparentTemperature) ? value.temperature : value.apparentTemperature;
  if (value.windSpeed >= 9.0F) feels -= 2.0F;
  if (feels < 3.0F) return COAT;
  if (feels < 10.0F) return JACKET;
  if (feels < 17.0F) return SWEATER;
  if (feels < 23.0F) return SHIRT;
  return SHORT_SLEEVE;
}

uint16_t outfitColor(OutfitKind outfit) {
  switch (outfit) {
    case SHIRT: return 0x4D5F;
    case SWEATER: return 0xA40F;
    case JACKET: return 0xB3C7;
    case COAT: return 0x294D;
    case RAINCOAT: return 0xFDE0;
    default: return 0xF986;
  }
}

void drawRider(const WeatherState &value) {
  // The display remains at 24 FPS, while the 24-pose pedal cycle advances at
  // About 8 poses/second. One complete pedal cycle takes roughly 3 seconds.
  int frame = (riderAnimationQ8 >> 8) % RIDER_FRAMES;
  const uint16_t *image = RIDER_IMAGES[frame];
  const uint8_t *mask = RIDER_MASKS[frame];
  OutfitKind outfit = adminSettings.outfitMode == 1
                          ? static_cast<OutfitKind>(min<uint8_t>(adminSettings.manualOutfit, RAINCOAT))
                          : recommendedOutfit(value);
  uint16_t clothing = outfitColor(outfit);
  constexpr int riderX = 87;
  constexpr int riderY = 62;
  for (int y = 0; y < RIDER_HEIGHT; ++y) {
    for (int x = 0; x < RIDER_WIDTH; ++x) {
      int index = y * RIDER_WIDTH + x;
      uint8_t bit = pgm_read_byte(&mask[index >> 3]) & (0x80 >> (index & 7));
      if (!bit) continue;
      uint16_t color = pgm_read_word(&image[index]);
      uint8_t r = (color >> 11) & 31;
      uint8_t g = (color >> 5) & 63;
      uint8_t b = color & 31;
      bool torsoFabric = x >= 22 && x <= 43 && y >= 16 && y <= 32 &&
                         r >= 17 && r * 2 > g && r > b + 5;
      frameBuffer.drawPixel(riderX + x, riderY + y, torsoFabric ? clothing : color);
    }
  }
  if (outfit == RAINCOAT) {
    frameBuffer.drawCircle(riderX + 38, riderY + 12, 7, 0xFDE0);
    frameBuffer.drawLine(riderX + 22, riderY + 25, riderX + 17, riderY + 31, 0xFDE0);
  } else if (outfit == COAT) {
    frameBuffer.drawLine(riderX + 34, riderY + 19, riderX + 45, riderY + 19, 0xD9A0);
  }
  riderAnimationQ8 += scaledAnimationStep(RIDER_STEP_Q8, adminSettings.riderSpeed);
}

uint16_t bichonSceneColor(uint16_t color) {
  uint16_t tinted = color;
  switch (adminSettings.dogTint) {
    case 1: tinted = blend565(tinted, 0xFEEA, 34); break;  // Warm white.
    case 2: tinted = blend565(tinted, 0xFE2D, 52); break;  // Cream.
    case 3: tinted = blend565(tinted, 0xDFFF, 36); break;  // Cool snowy white.
    case 4: tinted = blend565(tinted, 0xB5B6, 62); break;  // Silver gray.
    default: break;
  }
  if (currentScene == NIGHT) {
    uint8_t shade = static_cast<uint8_t>(map(adminSettings.dogNightBright, 40, 100, 78, 28));
    return blend565(tinted, 0x214B, shade);
  }
  if (currentScene == SUNSET) return blend565(tinted, 0xFBA8, 24);
  if (currentScene == RAIN) return blend565(tinted, 0x422F, 44);
  return tinted;
}

void drawBichonPixels(const uint8_t *pixels, int x, int y) {
  for (int py = 0; py < BICHON_HEIGHT; ++py) {
    for (int px = 0; px < BICHON_WIDTH; ++px) {
      int index = py * BICHON_WIDTH + px;
      uint8_t paletteIndex = pgm_read_byte(&pixels[index]);
      if (paletteIndex == 0) continue;
      uint16_t color = pgm_read_word(&BICHON_PALETTE[paletteIndex]);
      frameBuffer.drawPixel(x + px, y + py, bichonSceneColor(color));
    }
  }
}

void drawRunningBichon() {
  uint8_t frame = (dogAnimationQ8 >> 8) % BICHON_RUN_FRAMES;
  uint8_t nextFrame = (frame + 1) % BICHON_RUN_FRAMES;
  uint8_t transition = dogAnimationQ8 & 0xFF;
  float cycle = static_cast<float>(dogAnimationQ8 % (BICHON_RUN_FRAMES * 256UL)) /
                (BICHON_RUN_FRAMES * 256.0F);
  int bob = static_cast<int>(roundf(sinf(cycle * TWO_PI * 2.0F)));
  int foreAft = static_cast<int>(roundf(sinf(cycle * TWO_PI) * 0.65F));
  int spriteX = 56 + foreAft;
  int spriteY = 90 + bob;
  const uint8_t *currentPixels = BICHON_RUN + frame * BICHON_WIDTH * BICHON_HEIGHT;
  const uint8_t *nextPixels = BICHON_RUN + nextFrame * BICHON_WIDTH * BICHON_HEIGHT;
  for (int py = 0; py < BICHON_HEIGHT; ++py) {
    for (int px = 0; px < BICHON_WIDTH; ++px) {
      int index = py * BICHON_WIDTH + px;
      uint8_t currentIndex = pgm_read_byte(&currentPixels[index]);
      uint8_t nextIndex = pgm_read_byte(&nextPixels[index]);
      if (currentIndex == 0 && nextIndex == 0) continue;
      int x = spriteX + px;
      int y = spriteY + py;
      if (currentIndex != 0 && nextIndex != 0) {
        uint16_t currentColor = bichonSceneColor(pgm_read_word(&BICHON_PALETTE[currentIndex]));
        uint16_t nextColor = bichonSceneColor(pgm_read_word(&BICHON_PALETTE[nextIndex]));
        frameBuffer.drawPixel(x, y, blend565(currentColor, nextColor, transition));
      } else if (currentIndex != 0) {
        uint16_t color = bichonSceneColor(pgm_read_word(&BICHON_PALETTE[currentIndex]));
        blendFramePixel(x, y, color, 255 - transition);
      } else {
        uint16_t color = bichonSceneColor(pgm_read_word(&BICHON_PALETTE[nextIndex]));
        blendFramePixel(x, y, color, transition);
      }
    }
  }
  dogAnimationQ8 += scaledAnimationStep(DOG_STEP_Q8, adminSettings.dogSpeed);
}

void drawSleepingBichon() {
  int breathing = static_cast<int>(roundf((sinf(animationFrame * 0.10F) + 1.0F) * 0.5F));
  drawBichonPixels(BICHON_SLEEP, 92, 93 + breathing);
}

bool travelerResting(const WeatherState &value, float hour) {
  float wakeHour = value.valid ? value.sunriseMinutes / 60.0F : 5.5F;
  return hour >= 0.0F && hour < wakeHour;
}

void drawCamp() {
  constexpr int campX = 46;
  constexpr int campY = 53;
  frameBuffer.drawRGBBitmap(campX, campY, CAMP_IMAGE, CAMP_MASK, CAMP_WIDTH, CAMP_HEIGHT);
  int flicker = (animationFrame / 2) % 3;
  int fireX = campX + 12;
  int fireBaseY = campY + 48;
  frameBuffer.fillTriangle(fireX - 2, fireBaseY, fireX + 3, fireBaseY,
                           fireX + (flicker - 1), fireBaseY - 7 - flicker, WARM);
  frameBuffer.fillTriangle(fireX, fireBaseY, fireX + 3, fireBaseY,
                           fireX + 1, fireBaseY - 5, 0xFFE0);
  for (int i = 0; i < 3; ++i) {
    int sparkY = fireBaseY - 10 - ((animationFrame + i * 7) % 11);
    int sparkX = fireX + ((animationFrame / 3 + i * 3) % 7) - 3;
    frameBuffer.drawPixel(sparkX, sparkY, i == 0 ? WHITE : WARM);
  }
}

void drawWeatherEffects(const WeatherState &value) {
  bool rain = currentScene == RAIN || isRain(value.code) || value.rain > 0.05F;
  bool snow = currentScene == SNOW || isSnow(value.code) || value.snowfall > 0.02F;
  bool thunder = isThunderstorm(value.code);
  bool hail = isHail(value.code);
  bool freezing = isFreezingRain(value.code);
  bool shower = isRainShower(value.code) || isSnowShower(value.code);
  bool typhoon = rain && value.windGust >= 25.0F;
  bool blizzard = snow && value.windGust >= 18.0F;
  bool fog = value.code == 45 || value.code == 48 ||
             (!isnan(value.visibility) && value.visibility <= 1000.0F);
  bool haze = hazeAir(value) && !rain && !snow;
  bool dusty = dustyAir(value);
  bool sandstorm = sandstormAir(value);

  if (fog || haze) {
    uint16_t color = fog ? (value.code == 48 ? 0xAEBF : 0xBDF7) : 0xAD53;
    uint8_t alpha = fog ? 82 : 48;
    for (int band = 0; band < (fog ? 8 : 6); ++band) {
      int y = 29 + band * 10 + static_cast<int>(sinf((animationFrame + band * 17) * 0.055F) * 2.0F);
      int offset = (animationFrame / (fog ? 3 : 5) + band * 29) % 36;
      for (int x = -36 + offset; x < SCENE_WIDTH; x += 54) {
        for (int dx = 0; dx < 28; ++dx) blendFramePixel(x + dx, y, color, alpha);
        if (band & 1) {
          for (int dx = 0; dx < 18; ++dx) blendFramePixel(x + dx + 10, y + 2, color, alpha / 2);
        }
      }
    }
  }

  if (dusty || sandstorm) {
    uint16_t sand = sandstorm ? 0xD489 : 0xC565;
    int dustCount = sandstorm ? 62 : 34;
    int dustSpeed = sandstorm ? 9 : 4;
    for (int i = 0; i < dustCount; ++i) {
      int x = (i * 37 + animationFrame * dustSpeed + (i * i) % 19) % 270 - 15;
      int y = 28 + (i * 23 + (animationFrame / 2)) % 80;
      int wave = static_cast<int>(sinf((animationFrame + i * 11) * 0.13F) * (sandstorm ? 4.0F : 2.0F));
      blendFramePixel(x, y + wave, sand, sandstorm ? 175 : 120);
      if (i % 4 == 0) frameBuffer.drawLine(x - 4, y + wave, x + 5, y + wave - 1, sand);
    }
  }

  if (rain) {
    // Three depth layers: distant rain is short and slow; foreground drops
    // are longer, brighter and faster. Per-drop phases prevent synchronized
    // rows, while the sideways drift gives a gentle changing wind direction.
    int dropCount = constrain(static_cast<int>(22 + value.precipitation * 20.0F +
                                                value.windSpeed * 1.8F +
                                                (shower ? 14 : 0) + (typhoon ? 18 : 0)),
                              26, 94);
    int windSlant = constrain(static_cast<int>(value.windSpeed / 3.0F) + (typhoon ? 3 : 0), 1, 11);
    for (int i = 0; i < dropCount; ++i) {
      int layer = i % 3;
      int speed = 3 + layer * 2 + ((i / 3) & 1) + (shower ? 1 : 0) + (typhoon ? 2 : 0);
      int length = 3 + layer * 2 + (typhoon ? 2 : 0);
      int fall = static_cast<int>(animationFrame * speed + i * 47 + i * i * 3);
      int y = 19 + (fall % 101);
      int wind = static_cast<int>((animationFrame / 96 + i * 5) % 9) - 4;
      int x = (i * 67 + i * i * 11 + fall * windSlant / 8 + wind + 256) % 256 - 8;
      uint16_t color = layer == 0 ? RAIN_FAINT : (layer == 1 ? RAIN_MID : RAIN_BLUE);
      frameBuffer.drawLine(x, y, x - windSlant - layer, y + length, color);
      if (freezing && layer == 2 && y > 84) frameBuffer.drawPixel(x - windSlant, y + length + 1, CYAN);
      if (layer == 2 && y + length >= 108) {
        int splashAge = (fall / 3 + i * 7) % 12;
        if (splashAge < 3) {
          int splashY = 110 + (i & 1);
          uint16_t splash = freezing ? CYAN : RAIN_MID;
          frameBuffer.drawPixel(x - splashAge - 1, splashY - splashAge, splash);
          frameBuffer.drawPixel(x + splashAge + 1, splashY - splashAge, splash);
          if (splashAge == 0) frameBuffer.drawPixel(x, splashY, freezing ? WHITE : RAIN_BLUE);
        }
      }
    }
    if (freezing) {
      for (int x = 5; x < 235; x += 18) {
        int shimmer = (animationFrame + x) % 48;
        if (shimmer < 8) blendFramePixel(x, 110, WHITE, 80);
      }
    }
  }

  if (hail) {
    for (int i = 0; i < 26; ++i) {
      int speed = 4 + (i % 3);
      int fall = animationFrame * speed + i * 31;
      int x = (i * 53 + fall / 4) % 252 - 6;
      int y = 22 + (fall % 92);
      frameBuffer.fillCircle(x, y, 1, WHITE);
      frameBuffer.drawPixel(x, y, 0xB71F);
      if (y > 104 && ((fall / 3) % 5) < 2) frameBuffer.drawPixel(x + ((i & 1) ? 2 : -2), 109, CYAN);
    }
  }

  if (thunder && animationFrame % 128 < 5) {
    uint8_t flash = animationFrame % 128 < 2 ? 70 : 38;
    for (int y = 24; y < 80; y += 2) {
      for (int x = 0; x < SCENE_WIDTH; x += 2) blendFramePixel(x, y, WHITE, flash);
    }
    int boltX = 172 + static_cast<int>((animationFrame / 128) % 3) * 14;
    frameBuffer.drawLine(boltX, 25, boltX - 9, 45, WHITE);
    frameBuffer.drawLine(boltX - 9, 45, boltX - 2, 44, WHITE);
    frameBuffer.drawLine(boltX - 2, 44, boltX - 14, 68, WARM);
  }

  if (snow) {
    int snowCount = constrain(static_cast<int>(22 + value.snowfall * 18.0F +
                                               (blizzard ? 28 : 0) + (shower ? 10 : 0)),
                              24, 82);
    for (int i = 0; i < snowCount; ++i) {
      if (blizzard) {
        int speed = 6 + (i % 4);
        int x = (i * 43 + animationFrame * speed) % 270 - 15;
        int y = 25 + (i * 19 + animationFrame * (1 + i % 2)) % 88;
        frameBuffer.drawLine(x, y, x + 5 + value.windSpeed / 2, y - 1, WHITE);
      } else {
        int drift = ((i % 3) - 1) * static_cast<int>((animationFrame / 5) % 7);
        int x = (i * 43 + drift + 240) % 240;
        int y = 22 + ((i * 19 + animationFrame * (1 + i % 2)) % 96);
        frameBuffer.fillCircle(x, y, i % 4 == 0 ? 2 : 1, WHITE);
      }
    }
  }

  if (value.windSpeed >= 7.0F || value.windGust >= 12.0F) {
    int particleCount = constrain(static_cast<int>(value.windGust), 10, 34);
    int windStep = constrain(static_cast<int>(value.windSpeed * 1.8F) + (typhoon ? 14 : 0), 8, 54);
    for (int i = 0; i < particleCount; ++i) {
      int x = (i * 41 + animationFrame * windStep / 4) % 270 - 15;
      int wave = static_cast<int>(4.0F * sinf((animationFrame + i * 13) * 0.11F));
      int y = 35 + (i * 29) % 70 + wave;
      uint16_t color = i % 3 == 0 ? 0xA5A6 : 0x6BE3;
      frameBuffer.drawLine(x, y, x + 4 + windStep / 8, y - 1, color);
      if (i % 5 == 0) frameBuffer.fillTriangle(x, y, x + 3, y - 2, x + 4, y + 2, 0x7BC5);
    }
    if (value.windGust >= 25.0F) {
      for (int i = 0; i < 5; ++i) {
        int y = 31 + i * 17 + ((animationFrame + i * 11) % 9);
        frameBuffer.drawFastHLine((animationFrame * 5 + i * 47) % 180, y, 42, RAIN_FAINT);
      }
      if (typhoon) {
        int cx = 184;
        int cy = 62;
        for (int a = 0; a < 18; ++a) {
          float angle = (animationFrame * 0.08F + a * 0.62F);
          int r = 5 + a;
          int x = cx + static_cast<int>(cosf(angle) * r);
          int y = cy + static_cast<int>(sinf(angle) * r * 0.55F);
          blendFramePixel(x, y, RAIN_FAINT, 160);
        }
      }
    }
  }
}

void drawWifiStatus(int x, int y) {
  bool connected = WiFi.status() == WL_CONNECTED;
  uint16_t color = connected ? CYAN : MUTED;
  frameBuffer.drawLine(x, y + 2, x + 6, y, color);
  frameBuffer.drawLine(x + 6, y, x + 12, y + 2, color);
  frameBuffer.drawLine(x + 3, y + 5, x + 6, y + 3, color);
  frameBuffer.drawLine(x + 6, y + 3, x + 9, y + 5, color);
  frameBuffer.fillCircle(x + 6, y + 8, 1, color);
  if (!connected) frameBuffer.drawLine(x + 1, y, x + 11, y + 9, RED);
}

void drawSmallCloud(int x, int y, uint16_t color) {
  frameBuffer.fillCircle(x + 5, y + 6, 4, color);
  frameBuffer.fillCircle(x + 10, y + 4, 5, color);
  frameBuffer.fillCircle(x + 15, y + 7, 4, color);
  frameBuffer.fillRect(x + 4, y + 6, 12, 5, color);
}

void drawRichCloud(int x, int y, uint16_t base, uint16_t shade) {
  drawSmallCloud(x + 1, y + 2, shade);
  drawSmallCloud(x, y, base);
  frameBuffer.drawPixel(x + 5, y + 4, WHITE);
  frameBuffer.drawFastHLine(x + 7, y + 3, 4, WHITE);
}

void drawSunGlyph(int x, int y) {
  frameBuffer.fillCircle(x + 10, y + 9, 5, WARM);
  frameBuffer.drawCircle(x + 10, y + 9, 6, 0xFDB3);
  frameBuffer.drawFastHLine(x + 1, y + 9, 4, WARM);
  frameBuffer.drawFastHLine(x + 15, y + 9, 4, WARM);
  frameBuffer.drawFastVLine(x + 10, y, 4, WARM);
  frameBuffer.drawFastVLine(x + 10, y + 15, 4, WARM);
  frameBuffer.drawLine(x + 4, y + 3, x + 6, y + 5, WARM);
  frameBuffer.drawLine(x + 16, y + 3, x + 14, y + 5, WARM);
  frameBuffer.drawLine(x + 4, y + 15, x + 6, y + 13, WARM);
  frameBuffer.drawLine(x + 16, y + 15, x + 14, y + 13, WARM);
}

void drawRainDrops(int x, int y, uint8_t count, uint8_t slant) {
  for (uint8_t i = 0; i < count; ++i) {
    int dx = 4 + i * 5;
    int dy = 13 + (i & 1);
    frameBuffer.drawLine(x + dx, y + dy, x + dx - slant, y + dy + 5, RAIN_BLUE);
    frameBuffer.drawPixel(x + dx - slant, y + dy + 6, RAIN_MID);
  }
}

void drawSnowFlakes(int x, int y, uint8_t count) {
  for (uint8_t i = 0; i < count; ++i) {
    int cx = x + 5 + i * 5;
    int cy = y + 15 + (i & 1);
    frameBuffer.drawPixel(cx, cy, WHITE);
    frameBuffer.drawFastHLine(cx - 1, cy, 3, CYAN);
    frameBuffer.drawFastVLine(cx, cy - 1, 3, CYAN);
  }
}

void drawHailDots(int x, int y) {
  for (uint8_t i = 0; i < 3; ++i) {
    frameBuffer.fillCircle(x + 5 + i * 5, y + 15 + (i & 1), 1, WHITE);
    frameBuffer.drawPixel(x + 5 + i * 5, y + 15 + (i & 1), 0xDFFF);
  }
}

void drawLightning(int x, int y) {
  frameBuffer.fillTriangle(x + 10, y + 9, x + 6, y + 16, x + 10, y + 15, WARM);
  frameBuffer.fillTriangle(x + 9, y + 14, x + 14, y + 13, x + 8, y + 20, WARM);
}

void drawWindGlyph(int x, int y, uint16_t color) {
  frameBuffer.drawFastHLine(x + 1, y + 5, 12, color);
  frameBuffer.drawPixel(x + 14, y + 4, color);
  frameBuffer.drawFastHLine(x + 5, y + 9, 13, color);
  frameBuffer.drawPixel(x + 18, y + 10, color);
  frameBuffer.drawFastHLine(x + 2, y + 13, 14, color);
  frameBuffer.drawPixel(x + 16, y + 12, color);
}

void drawDustGlyph(int x, int y, bool storm) {
  uint16_t sand = storm ? 0xCB24 : 0xDD69;
  drawWindGlyph(x, y + 1, sand);
  for (uint8_t i = 0; i < (storm ? 10 : 6); ++i) {
    int px = x + 5 + (i * 3) % 14;
    int py = y + 3 + (i * 5) % 12;
    frameBuffer.drawPixel(px, py, sand);
  }
}

void drawFogGlyph(int x, int y, uint16_t color) {
  frameBuffer.drawFastHLine(x + 1, y + 5, 18, color);
  frameBuffer.drawFastHLine(x + 4, y + 9, 15, color);
  frameBuffer.drawFastHLine(x + 1, y + 13, 14, color);
  frameBuffer.drawFastHLine(x + 6, y + 17, 12, color);
}

bool dustyAir(const WeatherState &value) {
  return (!isnan(value.dust) && value.dust >= 40.0F) ||
         (!isnan(value.pm10) && value.pm10 >= 90.0F);
}

bool sandstormAir(const WeatherState &value) {
  return ((!isnan(value.dust) && value.dust >= 180.0F) ||
          (!isnan(value.pm10) && value.pm10 >= 250.0F)) &&
         (value.windGust >= 12.0F || value.windSpeed >= 8.0F);
}

bool hazeAir(const WeatherState &value) {
  return (!isnan(value.aerosolOpticalDepth) && value.aerosolOpticalDepth >= 0.8F) ||
         (!isnan(value.pm10) && value.pm10 >= 75.0F) ||
         (!isnan(value.visibility) && value.visibility > 1000.0F && value.visibility < 5000.0F);
}

void drawWeatherIcon(const WeatherState &value, int x, int y) {
  bool rain = isRain(value.code) || value.rain > 0.05F;
  bool snow = isSnow(value.code) || value.snowfall > 0.02F;
  bool fog = value.code == 45 || value.code == 48 ||
             (!isnan(value.visibility) && value.visibility <= 1000.0F);
  bool windy = value.windGust >= 15.0F || value.windSpeed >= 9.0F;
  if (isHail(value.code)) {
    drawRichCloud(x, y, 0x9CF3, 0x6B6D);
    drawLightning(x, y);
    drawHailDots(x, y);
  } else if (isThunderstorm(value.code)) {
    drawRichCloud(x, y, 0x9CF3, 0x6B6D);
    drawRainDrops(x, y, 2, 2);
    drawLightning(x, y);
  } else if (sandstormAir(value)) {
    drawDustGlyph(x, y, true);
  } else if (dustyAir(value) && windy) {
    drawDustGlyph(x, y, false);
  } else if (rain && value.windGust >= 25.0F) {
    drawWindGlyph(x, y, CYAN);
    drawRainDrops(x + 1, y + 1, 3, 3);
  } else if (snow && value.windGust >= 18.0F) {
    drawWindGlyph(x, y, CYAN);
    drawSnowFlakes(x, y, 3);
  } else if (rain && snow) {
    drawRichCloud(x, y, 0xD69B, 0x8C71);
    drawRainDrops(x, y, 2, 2);
    drawSnowFlakes(x + 2, y, 2);
  } else if (snow) {
    drawRichCloud(x, y, value.code >= 75 || value.snowfall >= 2.0F ? 0x9CF3 : 0xD69B, 0x8C71);
    drawSnowFlakes(x, y, value.code >= 75 || value.snowfall >= 2.0F ? 4 : 3);
  } else if (isFreezingRain(value.code)) {
    drawRichCloud(x, y, 0xD69B, 0x8C71);
    drawRainDrops(x, y, 2, 1);
    frameBuffer.drawFastHLine(x + 4, y + 19, 13, CYAN);
  } else if (rain) {
    if (isRainShower(value.code) && value.code <= 81) drawSunGlyph(x - 2, y - 1);
    drawRichCloud(x, y, value.precipitation >= 4.0F || value.code == 65 || value.code == 82 ? 0x8C71 : MUTED, 0x6B6D);
    drawRainDrops(x, y, value.precipitation >= 4.0F || value.code >= 65 || value.code == 82 ? 4 : 3,
                  value.windSpeed >= 7.0F ? 3 : 2);
  } else if (fog) {
    drawFogGlyph(x, y, value.code == 48 ? CYAN : MUTED);
  } else if (hazeAir(value)) {
    drawFogGlyph(x, y, 0xB5B1);
    frameBuffer.drawPixel(x + 4, y + 7, 0xDD69);
    frameBuffer.drawPixel(x + 14, y + 12, 0xDD69);
  } else if (windy) {
    drawWindGlyph(x, y, CYAN);
  } else if (value.code == 0) {
    drawSunGlyph(x, y);
  } else {
    if (value.code <= 2) {
      drawSunGlyph(x - 4, y - 2);
    }
    drawRichCloud(x + 1, y + 3, value.code == 3 ? MUTED : 0xD69B, 0x8C71);
  }
}

const char *weatherLabel(const WeatherState &value) {
  bool rain = isRain(value.code) || value.rain > 0.05F;
  bool snow = isSnow(value.code) || value.snowfall > 0.02F;
  bool fog = value.code == 45 || value.code == 48 ||
             (!isnan(value.visibility) && value.visibility <= 1000.0F);
  if (isHail(value.code)) return "冰雹";
  if (sandstormAir(value)) return "沙尘暴";
  if (dustyAir(value) && (value.windGust >= 12.0F || value.windSpeed >= 8.0F)) return "扬沙";
  if (dustyAir(value)) return "浮尘";
  if (hazeAir(value) && !rain && !snow) return "霾";
  if (rain && snow) return "雨夹雪";
  if (snow && value.windGust >= 18.0F) return "暴风雪";
  if (rain && value.windGust >= 25.0F) return "台风";
  if (!rain && !snow && value.windGust >= 25.0F) return "暴风";
  if (isThunderstorm(value.code)) return "雷阵雨";
  if (value.code >= 1 && value.code <= 3 && value.nextCode == 0) return "多云转晴";
  if (value.code == 0 && value.nextCode >= 2 && value.nextCode <= 3) return "晴转多云";
  if (snow) {
    if (isSnowShower(value.code)) return "阵雪";
    if (value.code == 71 || value.code == 77 || value.snowfall < 0.8F) return "小雪";
    if (value.code == 73 || value.snowfall < 2.0F) return "中雪";
    return "大雪";
  }
  if (isFreezingRain(value.code)) return "冻雨";
  if (rain) {
    if (isRainShower(value.code)) return value.code == 82 ? "强阵雨" : "阵雨";
    if (value.precipitation >= 8.0F || value.code == 65) return "暴雨";
    if (value.precipitation >= 4.0F) return "大雨";
    if (value.precipitation >= 1.5F || value.code == 63) return "中雨";
    return "小雨";
  }
  if (fog) return value.code == 48 ? "冻雾" : "雾";
  if (value.windGust >= 20.0F) return "狂风";
  if (value.windGust >= 15.0F) return "大风";
  if (value.code == 0) return "晴";
  if (value.code == 1) return "晴间多云";
  if (value.code <= 2) return "多云";
  if (value.code == 3) return "阴";
  return "天气";
}

uint16_t birdColor(uint8_t palette, uint8_t shade) {
  constexpr uint16_t colors[][3] = {
      {0x8E4B, 0x5C85, 0xC72F},  // Green.
      {0xFDB1, 0xFB07, 0xFFE8},  // Orange.
      {0xF990, 0xE987, 0xFDB5},  // Pink.
      {0x8E9F, 0x4C7A, 0xC71F},  // Blue.
      {0xFECA, 0xF5A4, 0xFFFF},  // Yellow.
      {0xD69B, 0x9CD3, 0xFFFF},  // White-gray.
      {0xAD2F, 0x7BE8, 0xE75D},  // Mint.
      {0xFC10, 0xE9C8, 0xFDB7},  // Coral.
      {0xB5FF, 0x747F, 0xEFFF},  // Sky.
      {0xFDF4, 0xBB8B, 0xFFFF},  // Cream.
  };
  return colors[palette % 10][shade % 3];
}

bool birdWeatherAllowed(const WeatherState &value, float hour) {
  float minute = hour * 60.0F;
  if (minute < value.sunriseMinutes + 45 || minute > value.sunsetMinutes - 45) return false;
  if (value.code < 0) return false;
  if (isRain(value.code) || isSnow(value.code) || isThunderstorm(value.code) ||
      isHail(value.code) || isFreezingRain(value.code)) return false;
  if (value.rain > 0.05F || value.precipitation > 0.4F || value.snowfall > 0.02F) return false;
  if (value.windGust >= 15.0F || value.windSpeed >= 9.0F) return false;
  if (value.code == 45 || value.code == 48 || dustyAir(value) || hazeAir(value) ||
      sandstormAir(value)) return false;
  return true;
}

void planBirdPassesForHour(const struct tm &now) {
  if (birdScheduleDay == now.tm_yday && birdScheduleHour == now.tm_hour) return;
  birdScheduleDay = now.tm_yday;
  birdScheduleHour = now.tm_hour;
  birdPassCount = random(0, MAX_BIRD_PASSES_PER_HOUR + 1);
  for (uint8_t i = 0; i < MAX_BIRD_PASSES_PER_HOUR; ++i) birdPassMinutes[i] = 255;
  for (uint8_t i = 0; i < birdPassCount; ++i) {
    uint8_t minute;
    bool unique;
    do {
      minute = random(0, 60);
      unique = true;
      for (uint8_t j = 0; j < i; ++j) {
        if (birdPassMinutes[j] == minute) unique = false;
      }
    } while (!unique);
    birdPassMinutes[i] = minute;
  }
}

void startBirdFlock() {
  birdFlockActive = true;
  birdFlockStartFrame = animationFrame;
  birdFlockSize = random(2, MAX_BIRDS_PER_FLOCK + 1);
  for (uint8_t i = 0; i < MAX_BIRDS_PER_FLOCK; ++i) {
    birdFlockColors[i] = random(0, 10);
    birdFlockKinds[i] = random(0, 3);
    birdFlockXOffsets[i] = -static_cast<int8_t>(i * random(10, 15) + random(0, 7));
    birdFlockYOffsets[i] = static_cast<int8_t>(random(-7, 8));
  }
}

void drawSingleBird(int x, int y, uint8_t palette, uint8_t kind, uint8_t flap) {
  uint16_t main = birdColor(palette, 0);
  uint16_t shade = birdColor(palette, 1);
  uint16_t light = birdColor(palette, 2);
  constexpr int8_t wingLiftByFrame[] = {-6, -5, -3, -1, 2, 5, 3, 1, -2};
  int wingLift = wingLiftByFrame[flap % 9];
  int wingTipY = y + wingLift;
  int tailY = y + (kind == 1 ? 1 : 0);

  // Forked tail and tapered body.
  frameBuffer.fillTriangle(x - 5, tailY - 1, x - 9, tailY - 4, x - 8, tailY, shade);
  frameBuffer.fillTriangle(x - 5, tailY + 1, x - 9, tailY + 4, x - 7, tailY + 2, main);
  frameBuffer.fillTriangle(x - 3, y - 2, x + 5, y - 1, x + 1, y + 4, main);
  frameBuffer.fillCircle(x - 1, y, 3, main);
  frameBuffer.fillCircle(x + 4, y - 1, 2, main);
  frameBuffer.drawLine(x - 3, y + 2, x + 3, y + 2, shade);
  frameBuffer.fillTriangle(x + 6, y - 1, x + 9, y, x + 6, y + 1, 0xFB20);
  frameBuffer.drawPixel(x + 4, y - 2, 0x2945);

  // Two wings: top wing changes with the 9-frame flap, lower wing counter-balances.
  frameBuffer.fillTriangle(x - 1, y - 1, x - 5, wingTipY, x + 4, y - 1, shade);
  frameBuffer.fillTriangle(x + 1, y + 1, x - 3, y + 5 - wingLift / 2, x + 4, y + 2, main);
  frameBuffer.drawLine(x - 4, wingTipY + 1, x - 2, wingTipY + 2, light);
  frameBuffer.drawLine(x - 2, wingTipY + 2, x + 1, wingTipY + 1, light);
  frameBuffer.drawPixel(x - 1, y + 3 - wingLift / 3, light);
  if (kind == 2) frameBuffer.drawPixel(x - 2, y + 2, light);
}

void updateBirdFlock(const WeatherState &value, float hour) {
  struct tm now{};
  if (!getLocalTime(&now, 5)) return;
  planBirdPassesForHour(now);
  if (birdFlockActive && animationFrame - birdFlockStartFrame > 175) {
    birdFlockActive = false;
  }
  if (!birdWeatherAllowed(value, hour) || birdFlockActive) return;
  int triggerKey = now.tm_yday * 1440 + now.tm_hour * 60 + now.tm_min;
  if (triggerKey == lastBirdTriggerKey) return;
  for (uint8_t i = 0; i < birdPassCount; ++i) {
    if (birdPassMinutes[i] == now.tm_min) {
      lastBirdTriggerKey = triggerKey;
      startBirdFlock();
      return;
    }
  }
}

void drawBirdFlock() {
  if (!birdFlockActive) return;
  uint32_t age = animationFrame - birdFlockStartFrame;
  int baseX = -20 + static_cast<int>(age * 2);
  if (baseX > SCENE_WIDTH + 35) {
    birdFlockActive = false;
    return;
  }
  int baseY = 43 + static_cast<int>(sinf(age * 0.055F) * 3.0F);
  for (uint8_t i = 0; i < birdFlockSize; ++i) {
    uint8_t flap = (age / 3 + i) % 9;
    int x = baseX + birdFlockXOffsets[i];
    int y = baseY + birdFlockYOffsets[i] + static_cast<int>(sinf((age + i * 11) * 0.10F) * 2.0F);
    drawSingleBird(x, y, birdFlockColors[i], birdFlockKinds[i], flap);
  }
}

float solarCurveAtMinute(float minute, int sunriseMinutes, int sunsetMinutes) {
  float sunrise = constrain(static_cast<float>(sunriseMinutes), 0.0F, 1439.0F);
  float sunset = constrain(static_cast<float>(sunsetMinutes), sunrise + 1.0F, 1439.0F);
  float daylightLength = sunset - sunrise;

  if (minute >= sunrise && minute <= sunset) {
    float daylightProgress = (minute - sunrise) / daylightLength;
    return sinf(daylightProgress * PI);
  }

  float nightLength = max(1.0F, 1440.0F - daylightLength);
  float nightProgress = minute > sunset
                            ? (minute - sunset) / nightLength
                            : (minute + 1440.0F - sunset) / nightLength;
  return -sinf(constrain(nightProgress, 0.0F, 1.0F) * PI);
}

void drawSunScheduleAtMinute(const WeatherState &value, int nowMinutes) {
  constexpr int left = 65;
  constexpr int right = 111;
  constexpr int top = 114;
  constexpr int bottom = 134;
  constexpr int horizonY = 124;
  constexpr int amplitude = 8;

  frameBuffer.drawFastHLine(left, horizonY, right - left + 1, 0x8410);

  int previousX = left;
  int previousY = horizonY;
  for (int x = left; x <= right; ++x) {
    float dayMinute = static_cast<float>(x - left) * 1439.0F / max(1, right - left);
    float altitude = solarCurveAtMinute(dayMinute, value.sunriseMinutes, value.sunsetMinutes);
    int y = horizonY - static_cast<int>(altitude * amplitude);
    y = constrain(y, top + 1, bottom - 1);
    uint16_t curveColor = altitude >= 0.0F ? 0xD69F : 0x5AEB;
    if (x > left) frameBuffer.drawLine(previousX, previousY, x, y, curveColor);
    previousX = x;
    previousY = y;
  }

  float dotMinute = constrain(static_cast<float>(nowMinutes), 0.0F, 1439.0F);
  float dotAltitude = solarCurveAtMinute(dotMinute, value.sunriseMinutes, value.sunsetMinutes);
  int dotX = left + static_cast<int>(dotMinute * (right - left) / 1439.0F);
  int dotY = horizonY - static_cast<int>(dotAltitude * amplitude);
  dotY = constrain(dotY, top + 1, bottom - 1);
  bool sunAbove = dotAltitude >= 0.0F;
  uint16_t glow = sunAbove ? 0x8E9F : 0x39E7;
  uint16_t dotColor = sunAbove ? WHITE : MUTED;
  frameBuffer.drawCircle(dotX, dotY, 4, 0x31AE);
  frameBuffer.drawCircle(dotX, dotY, 3, glow);
  frameBuffer.fillCircle(dotX, dotY, 2, dotColor);
}

void drawSunSchedule(const WeatherState &value, const struct tm &now, bool timeReady) {
  int nowMinutes = demoMode ? static_cast<int>(demoHour * 60.0F)
                            : (timeReady ? now.tm_hour * 60 + now.tm_min : 720);
  drawSunScheduleAtMinute(value, nowMinutes);
}

void drawPrecipitationChart(const WeatherState &value, const struct tm &now, bool timeReady) {
  constexpr int left = 117;
  constexpr int top = 115;
  constexpr int width = 52;
  constexpr int height = 19;
  constexpr int bottom = top + height - 1;
  constexpr int right = left + width - 1;

  frameBuffer.drawRect(left, top, width, height, 0x31AE);
  for (int quarter = 1; quarter < 4; ++quarter) {
    int gridY = bottom - (quarter * (height - 3)) / 4;
    frameBuffer.drawFastHLine(left + 1, gridY, width - 2, 0x212A);
    int gridX = left + 1 + (quarter * (width - 3)) / 4;
    frameBuffer.drawFastVLine(gridX, top + 1, height - 2, 0x212A);
  }

  uint8_t count = value.precipitationProbabilityCount;
  if (count < 2) {
    frameBuffer.drawFastHLine(left + 2, bottom - 2, width - 4, RAIN_MID);
  } else {
    int previousX = left + 1;
    int previousY = bottom - map(value.precipitationProbability[0], 0, 100, 0, height - 3);
    for (uint8_t i = 1; i < count; ++i) {
      int x = left + 1 + (static_cast<int>(i) * (width - 3)) / (count - 1);
      int y = bottom - map(value.precipitationProbability[i], 0, 100, 0, height - 3);
      int fillLeft = min(previousX, x);
      int fillRight = max(previousX, x);
      for (int sx = fillLeft; sx <= fillRight; ++sx) {
        float t = x == previousX ? 0.0F : static_cast<float>(sx - previousX) / (x - previousX);
        int sy = previousY + static_cast<int>((y - previousY) * t);
        frameBuffer.drawFastVLine(sx, sy + 1, max(0, bottom - sy - 1), 0x224D);
      }
      frameBuffer.drawLine(previousX, previousY, x, y, 0x5EFF);
      previousX = x;
      previousY = y;
    }
  }

  int nowMinutes = timeReady ? now.tm_hour * 60 + now.tm_min : 720;
  int markerX = left + 1 + (nowMinutes * (width - 3)) / 1439;
  markerX = constrain(markerX, left + 1, right - 1);
  frameBuffer.drawFastVLine(markerX, top, height, WHITE);

  int markerY = bottom - 2;
  if (count >= 2) {
    float nowHour = timeReady ? now.tm_hour + now.tm_min / 60.0F + now.tm_sec / 3600.0F : 12.0F;
    nowHour = constrain(nowHour, 0.0F, static_cast<float>(count - 1));
    int hour0 = static_cast<int>(floorf(nowHour));
    int hour1 = min(hour0 + 1, static_cast<int>(count - 1));
    float blend = nowHour - hour0;
    float probability =
        value.precipitationProbability[hour0] * (1.0F - blend) +
        value.precipitationProbability[hour1] * blend;
    markerY = bottom - map(static_cast<int>(roundf(probability)), 0, 100, 0, height - 3);
  }
  frameBuffer.fillCircle(markerX, markerY, 2, WHITE);
  frameBuffer.drawPixel(markerX, markerY, RAIN_BLUE);
}

float currentMoonPhase(bool timeReady) {
  if (!timeReady) return -1.0F;
  return moonPhaseFromEpoch(time(nullptr));
}

float currentMoonBrightLimbAngle(bool timeReady) {
  if (!timeReady) return 0.0F;
  return moonBrightLimbAngle(time(nullptr));
}

void drawMoonPhase(int centerX, int centerY, float phase, float brightLimbAngle) {
  constexpr int radius = 7;
  drawMoonSilhouette(centerX, centerY, radius, phase, 0xF7BE, false, true, brightLimbAngle);
}

uint16_t apparentTemperatureColor(float temperature) {
  constexpr float thresholds[] = {-10.0F, 10.0F, 16.0F, 20.0F, 24.0F, 28.0F, 31.0F, 36.0F};
  constexpr uint16_t colors[] = {
      0x5DDF,  // Ice blue.
      0x447B,  // Cool blue.
      0x6CF6,  // Mild blue-gray.
      0xA348,  // Comfortable warm brown.
      0xCC47,  // Amber.
      0xEB46,  // Orange warning.
      0xE228,  // Hot red.
      0xC106,  // Deep heat red.
  };
  if (temperature <= thresholds[0]) return colors[0];
  for (size_t i = 1; i < sizeof(thresholds) / sizeof(thresholds[0]); ++i) {
    if (temperature <= thresholds[i]) {
      float progress = (temperature - thresholds[i - 1]) / (thresholds[i] - thresholds[i - 1]);
      return blend565(colors[i - 1], colors[i], static_cast<uint8_t>(progress * 255.0F));
    }
  }
  return colors[sizeof(colors) / sizeof(colors[0]) - 1];
}

void drawThermometerIcon(int x, int y, float temperature) {
  uint16_t base = apparentTemperatureColor(temperature);
  uint16_t pale = blend565(base, WHITE, 156);
  uint16_t deep = blend565(base, BLACK, 42);
  uint16_t outline = blend565(base, WHITE, 205);

  frameBuffer.fillRoundRect(x + 2, y, 6, 11, 3, UI_NAVY);
  frameBuffer.fillRect(x + 4, y + 2, 1, 8, pale);
  frameBuffer.fillRect(x + 5, y + 2, 2, 8, deep);
  frameBuffer.drawRoundRect(x + 2, y, 6, 11, 3, outline);

  constexpr int bulbRadius = 4;
  int bulbX = x + 5;
  int bulbY = y + 11;
  for (int py = -bulbRadius; py <= bulbRadius; ++py) {
    for (int px = -bulbRadius; px <= bulbRadius; ++px) {
      if (px * px + py * py > bulbRadius * bulbRadius) continue;
      frameBuffer.drawPixel(bulbX + px, bulbY + py, px < 0 ? pale : deep);
    }
  }
  frameBuffer.drawCircle(bulbX, bulbY, bulbRadius, outline);
  frameBuffer.drawFastVLine(bulbX, bulbY - 3, 7, base);
  frameBuffer.drawPixel(bulbX - 2, bulbY - 2, blend565(pale, WHITE, 100));

  frameBuffer.drawFastHLine(x + 9, y + 2, 3, outline);
  frameBuffer.drawFastHLine(x + 9, y + 5, 2, outline);
  frameBuffer.drawFastHLine(x + 9, y + 8, 3, outline);
}

int temperatureDisplayX(const WeatherState &value) {
  char temperature[8];
  snprintf(temperature, sizeof(temperature), "%.0f", value.temperature);
  int temperatureWidth = strlen(temperature) * 12;
  int valueWidth = temperatureWidth + 7 + 12;
  return max(174, 238 - valueWidth);
}

void drawInterface(const WeatherState &value) {
  struct tm now{};
  bool timeReady = getLocalTime(&now, 5);
  frameBuffer.fillRect(0, 0, 240, 22, UI_NAVY);
  frameBuffer.fillRect(0, 113, 240, 22, UI_NAVY);

  char timeText[8] = "--:--";
  char dateText[12] = "--/--";
  char weekText[8] = "";
  if (timeReady) {
    snprintf(timeText, sizeof(timeText), now.tm_sec % 2 == 0 ? "%02d:%02d" : "%02d %02d",
             now.tm_hour, now.tm_min);
    const char *week[] = {"日", "一", "二", "三", "四", "五", "六"};
    snprintf(dateText, sizeof(dateText), "%02d/%02d", now.tm_mon + 1, now.tm_mday);
    snprintf(weekText, sizeof(weekText), "周%s", week[now.tm_wday]);
  }

  frameBuffer.setTextColor(WHITE);
  frameBuffer.setTextSize(2);
  frameBuffer.setCursor(4, 4);
  frameBuffer.print(timeText);
  frameBuffer.setTextSize(1);
  frameBuffer.setCursor(66, 7);
  frameBuffer.print(dateText);
  if (timeReady) drawChinese(weekText, 97, 16, WHITE);

  if (value.valid) {
    if (adminSettings.showTemperature) {
      char range[24];
      snprintf(range, sizeof(range), "%.0f~%.0f", value.dailyMin, value.dailyMax);
      frameBuffer.setTextColor(CYAN);
      frameBuffer.setTextSize(1);
      int rangeWidth = strlen(range) * 6;
      int rangeX = 166 - rangeWidth - 9;
      frameBuffer.setCursor(rangeX, 1);
      frameBuffer.print(range);
      frameBuffer.drawCircle(rangeX + rangeWidth + 2, 2, 1, CYAN);
      frameBuffer.setCursor(rangeX + rangeWidth + 5, 1);
      frameBuffer.print("C");
    }

    if (adminSettings.showFeelsLike) {
      char feels[8];
      snprintf(feels, sizeof(feels), "%.0f", value.apparentTemperature);
      int feelsWidth = strlen(feels) * 6;
      int feelsX = 166 - feelsWidth - 9;
      drawThermometerIcon(feelsX - 16, 6, value.apparentTemperature);
      frameBuffer.setTextColor(MUTED);
      frameBuffer.setTextSize(1);
      frameBuffer.setCursor(feelsX, 12);
      frameBuffer.print(feels);
      frameBuffer.drawCircle(feelsX + feelsWidth + 2, 13, 1, MUTED);
      frameBuffer.setCursor(feelsX + feelsWidth + 5, 12);
      frameBuffer.print("C");
    }

    const char *condition = weatherLabel(value);
    int conditionGlyphs = strlen(condition) / 3;
    drawChinese(condition, 216 - conditionGlyphs * 12, 16,
                conditionGlyphs >= 3 ? WARM : CYAN);
    drawWeatherIcon(value, 219, 1);
  }

  drawChinese("朝来市", 6, 129, WHITE);
  if (adminSettings.showWifi) drawWifiStatus(45, 119);
  if (value.valid) {
    if (holidayToday && adminSettings.showHoliday) drawChinese("祝日", 77, 129, WARM);
    else if (adminSettings.showSunSchedule) drawSunSchedule(value, now, timeReady);
    if (adminSettings.showPrecipitation) drawPrecipitationChart(value, now, timeReady);
    if (adminSettings.showMoon) {
      int moonX = constrain(temperatureDisplayX(value) - 9, 175, 184);
      drawMoonPhase(moonX, 124, currentMoonPhase(timeReady), currentMoonBrightLimbAngle(timeReady));
    }
  }

  if (value.valid && adminSettings.showTemperature) {
    char temperature[8];
    snprintf(temperature, sizeof(temperature), "%.0f", value.temperature);
    frameBuffer.setTextColor(WARM);
    frameBuffer.setTextSize(2);
    int temperatureWidth = strlen(temperature) * 12;
    int valueX = temperatureDisplayX(value);
    frameBuffer.setCursor(valueX, 116);
    frameBuffer.print(temperature);
    frameBuffer.drawCircle(valueX + temperatureWidth + 2, 118, 2, WARM);
    frameBuffer.setCursor(valueX + temperatureWidth + 7, 116);
    frameBuffer.print("C");
  } else {
    drawChinese("天气获取中", 164, 129, MUTED);
  }
}

void activateRandomWeatherDemo() {
  WeatherState base = weatherSnapshot();
  WeatherState value = base.valid ? base : WeatherState{};
  value.valid = true;
  value.sunriseMinutes = base.valid ? base.sunriseMinutes : 300;
  value.sunsetMinutes = base.valid ? base.sunsetMinutes : 1140;
  int type = random(0, 24);
  float hourChoices[] = {2.1F, 5.4F, 7.2F, 10.2F, 12.7F, 16.9F, 18.4F, 21.5F, 23.4F};
  demoHour = hourChoices[random(0, static_cast<long>(sizeof(hourChoices) / sizeof(hourChoices[0])))];
  value.temperature = random(-3, 35);
  value.apparentTemperature = value.temperature + random(-3, 4);
  value.dailyMin = value.temperature - random(2, 7);
  value.dailyMax = value.temperature + random(2, 8);
  value.precipitation = 0.0F;
  value.rain = 0.0F;
  value.snowfall = 0.0F;
  value.cloudCover = random(0, 85);
  value.windSpeed = random(0, 8);
  value.windGust = value.windSpeed + random(0, 8);
  value.visibility = 12000.0F;
  value.pm10 = 18.0F;
  value.dust = 2.0F;
  value.aerosolOpticalDepth = 0.18F;
  value.precipitationProbabilityCount = PRECIPITATION_HOURS;
  for (uint8_t i = 0; i < PRECIPITATION_HOURS; ++i) {
    value.precipitationProbability[i] = random(0, 35);
  }

  switch (type) {
    case 0: value.code = 0; value.nextCode = 0; value.cloudCover = random(0, 18); break;
    case 1: value.code = 1; value.nextCode = 2; value.cloudCover = random(18, 45); break;
    case 2: value.code = 0; value.nextCode = 3; value.cloudCover = random(22, 55); break;
    case 3: value.code = 2; value.nextCode = 0; value.cloudCover = random(40, 68); break;
    case 4: value.code = 3; value.nextCode = 3; value.cloudCover = random(86, 100); break;
    case 5: value.code = 45; value.nextCode = 45; value.cloudCover = random(70, 98); value.visibility = random(350, 1300); break;
    case 6: value.code = 48; value.nextCode = 48; value.cloudCover = random(80, 100); value.visibility = random(220, 900); value.temperature = random(-5, 3); value.apparentTemperature = value.temperature - 2; break;
    case 7: value.code = 1; value.nextCode = 2; value.cloudCover = random(40, 80); value.aerosolOpticalDepth = 0.95F; value.pm10 = random(80, 130); break;
    case 8: value.code = 1; value.nextCode = 1; value.cloudCover = random(30, 65); value.pm10 = random(95, 180); value.dust = random(42, 90); break;
    case 9: value.code = 1; value.nextCode = 2; value.cloudCover = random(35, 75); value.windSpeed = random(8, 13); value.windGust = random(14, 23); value.dust = random(55, 130); value.pm10 = random(120, 230); break;
    case 10: value.code = 2; value.nextCode = 2; value.cloudCover = random(55, 90); value.windSpeed = random(10, 16); value.windGust = random(18, 30); value.dust = random(190, 330); value.pm10 = random(260, 420); break;
    case 11: value.code = 61; value.nextCode = 61; value.rain = 0.4F; value.precipitation = 0.6F; value.cloudCover = random(75, 100); break;
    case 12: value.code = 63; value.nextCode = 63; value.rain = 1.6F; value.precipitation = 1.8F; value.cloudCover = random(82, 100); break;
    case 13: value.code = 65; value.nextCode = 65; value.rain = 5.5F; value.precipitation = 6.5F; value.cloudCover = random(90, 100); break;
    case 14: value.code = 82; value.nextCode = 82; value.rain = 8.5F; value.precipitation = 9.5F; value.cloudCover = random(82, 100); break;
    case 15: value.code = 66; value.nextCode = 67; value.rain = 1.2F; value.precipitation = 1.4F; value.temperature = random(-2, 4); value.apparentTemperature = value.temperature - 3; break;
    case 16: value.code = 71; value.nextCode = 71; value.snowfall = 0.35F; value.cloudCover = random(72, 100); value.temperature = random(-4, 4); value.apparentTemperature = value.temperature - 2; break;
    case 17: value.code = 73; value.nextCode = 75; value.snowfall = 1.2F; value.cloudCover = random(82, 100); value.temperature = random(-5, 2); value.apparentTemperature = value.temperature - 3; break;
    case 18: value.code = 86; value.nextCode = 86; value.snowfall = 2.4F; value.cloudCover = random(82, 100); value.temperature = random(-8, 0); value.apparentTemperature = value.temperature - 5; break;
    case 19: value.code = 95; value.nextCode = 95; value.rain = 3.2F; value.precipitation = 4.0F; value.cloudCover = random(82, 100); value.windGust = random(10, 20); break;
    case 20: value.code = 96; value.nextCode = 96; value.rain = 4.0F; value.precipitation = 5.2F; value.cloudCover = random(88, 100); value.windGust = random(12, 23); break;
    case 21: value.code = 82; value.nextCode = 95; value.rain = 9.0F; value.precipitation = 10.0F; value.cloudCover = 100; value.windSpeed = random(13, 20); value.windGust = random(27, 38); break;
    case 22: value.code = 86; value.nextCode = 86; value.snowfall = 3.0F; value.cloudCover = 100; value.temperature = random(-9, -1); value.apparentTemperature = value.temperature - 7; value.windSpeed = random(10, 17); value.windGust = random(19, 31); break;
    default: value.code = 80; value.nextCode = 81; value.rain = 1.1F; value.precipitation = 1.2F; value.cloudCover = random(45, 85); break;
  }
  if (isRain(value.code) || value.rain > 0.05F) {
    for (uint8_t i = 0; i < PRECIPITATION_HOURS; ++i) value.precipitationProbability[i] = random(45, 100);
  } else if (isSnow(value.code) || value.snowfall > 0.02F) {
    for (uint8_t i = 0; i < PRECIPITATION_HOURS; ++i) value.precipitationProbability[i] = random(35, 90);
  }
  demoWeather = value;
  demoMode = true;
  singleClickPending = false;
  previewMode = 0;
  updateSceneTarget(desiredScene(demoWeather, demoHour));
  if (birdWeatherAllowed(demoWeather, demoHour) && random(0, 100) < 72) {
    startBirdFlock();
  } else {
    birdFlockActive = false;
  }
  Serial.printf("随机天气演示：%s，%.1f 点，%.0f°C\n", weatherLabel(demoWeather), demoHour, demoWeather.temperature);
}

void restoreLiveWeatherMode() {
  demoMode = false;
  singleClickPending = false;
  previewMode = 0;
  birdFlockActive = false;
  requestWeather = true;
  Serial.println("已恢复实时天气模式");
}

String minuteText(int minutes) {
  minutes = (minutes % 1440 + 1440) % 1440;
  char text[6];
  snprintf(text, sizeof(text), "%02d:%02d", minutes / 60, minutes % 60);
  return String(text);
}

const char *moonPhaseLabel(float phase) {
  if (phase < 0.035F || phase > 0.965F) return "新月";
  if (phase < 0.22F) return "娥眉月";
  if (phase < 0.285F) return "上弦月";
  if (phase < 0.47F) return "盈凸月";
  if (phase < 0.535F) return "满月";
  if (phase < 0.72F) return "亏凸月";
  if (phase < 0.785F) return "下弦月";
  return "残月";
}

void activatePresetWeatherDemo(const String &scene) {
  if (scene == "live") {
    restoreLiveWeatherMode();
    return;
  }
  if (scene == "random") {
    activateRandomWeatherDemo();
    return;
  }

  WeatherState base = weatherSnapshot();
  WeatherState value = base.valid ? base : WeatherState{};
  value.valid = true;
  value.sunriseMinutes = base.valid ? base.sunriseMinutes : 300;
  value.sunsetMinutes = base.valid ? base.sunsetMinutes : 1140;
  value.temperature = base.valid ? base.temperature : 22.0F;
  value.apparentTemperature = base.valid ? base.apparentTemperature : value.temperature;
  value.dailyMin = base.valid ? base.dailyMin : value.temperature - 5.0F;
  value.dailyMax = base.valid ? base.dailyMax : value.temperature + 5.0F;
  value.precipitation = 0.0F;
  value.rain = 0.0F;
  value.snowfall = 0.0F;
  value.cloudCover = 20.0F;
  value.windSpeed = 3.0F;
  value.windGust = 6.0F;
  value.visibility = 12000.0F;
  value.pm10 = 18.0F;
  value.dust = 2.0F;
  value.aerosolOpticalDepth = 0.18F;
  value.precipitationProbabilityCount = PRECIPITATION_HOURS;
  for (uint8_t i = 0; i < PRECIPITATION_HOURS; ++i) value.precipitationProbability[i] = 10 + (i % 7) * 3;

  demoHour = 12.4F;
  if (scene == "sunny") {
    value.code = 0; value.nextCode = 1; value.cloudCover = 8.0F; value.temperature = 26.0F;
  } else if (scene == "cloudy") {
    value.code = 2; value.nextCode = 3; value.cloudCover = 62.0F; value.temperature = 22.0F;
  } else if (scene == "rain") {
    value.code = 63; value.nextCode = 63; value.cloudCover = 95.0F; value.rain = 2.0F;
    value.precipitation = 2.4F; value.temperature = 19.0F;
    for (uint8_t i = 0; i < PRECIPITATION_HOURS; ++i) value.precipitationProbability[i] = 55 + (i * 7) % 42;
  } else if (scene == "snow") {
    value.code = 73; value.nextCode = 75; value.cloudCover = 90.0F; value.snowfall = 1.4F;
    value.temperature = -1.0F; value.apparentTemperature = -4.0F;
    for (uint8_t i = 0; i < PRECIPITATION_HOURS; ++i) value.precipitationProbability[i] = 45 + (i * 5) % 48;
  } else if (scene == "night") {
    value.code = 1; value.nextCode = 1; value.cloudCover = 24.0F; demoHour = 22.3F;
  } else if (scene == "typhoon") {
    value.code = 82; value.nextCode = 95; value.cloudCover = 100.0F; value.rain = 9.0F;
    value.precipitation = 10.0F; value.windSpeed = 18.0F; value.windGust = 34.0F;
    value.temperature = 24.0F;
    for (uint8_t i = 0; i < PRECIPITATION_HOURS; ++i) value.precipitationProbability[i] = 78 + (i * 3) % 22;
  }
  value.dailyMin = value.temperature - 4.0F;
  value.dailyMax = value.temperature + 4.0F;
  value.apparentTemperature = isnan(value.apparentTemperature) ? value.temperature : value.apparentTemperature;
  demoWeather = value;
  demoMode = true;
  previewMode = 0;
  singleClickPending = false;
  birdFlockActive = false;
  updateSceneTarget(desiredScene(demoWeather, demoHour));
}

void sendJsonOk() {
  adminServer.sendHeader("Cache-Control", "no-store");
  adminServer.send(200, "application/json", "{\"ok\":true}");
}

bool argEnabled(const char *name, bool current) {
  if (!adminServer.hasArg(name)) return current;
  String value = adminServer.arg(name);
  return value == "1" || value == "true" || value == "on";
}

bool textHasControlBytes(const String &text) {
  for (size_t i = 0; i < text.length(); ++i) {
    uint8_t c = static_cast<uint8_t>(text[i]);
    if (c < 32 || c == 127) return true;
  }
  return false;
}

String safeJsonText(const String &text) {
  String safe;
  safe.reserve(text.length());
  for (size_t i = 0; i < text.length(); ++i) {
    uint8_t c = static_cast<uint8_t>(text[i]);
    if (c < 32 || c == 127) continue;
    safe += static_cast<char>(c);
  }
  return safe;
}

void deleteKnownNetwork(uint8_t removeIndex) {
  Preferences preferences;
  if (!preferences.begin("travelwifi", false)) return;
  uint8_t count = min(preferences.getUChar("count", 0), MAX_KNOWN_WIFI_NETWORKS);
  String ssids[MAX_KNOWN_WIFI_NETWORKS];
  String passwords[MAX_KNOWN_WIFI_NETWORKS];
  uint8_t kept = 0;
  for (uint8_t index = 0; index < count; ++index) {
    String ssid = preferences.getString(wifiPreferenceKey("ssid", index).c_str(), "");
    String password = preferences.getString(wifiPreferenceKey("pass", index).c_str(), "");
    if (index == removeIndex || ssid.isEmpty()) continue;
    ssids[kept] = ssid;
    passwords[kept] = password;
    ++kept;
  }
  preferences.clear();
  preferences.putUChar("count", kept);
  for (uint8_t index = 0; index < kept; ++index) {
    preferences.putString(wifiPreferenceKey("ssid", index).c_str(), ssids[index]);
    preferences.putString(wifiPreferenceKey("pass", index).c_str(), passwords[index]);
  }
  preferences.end();
  loadKnownNetworks();
}

void handleAdminRoot() {
  adminServer.send_P(200, "text/html; charset=utf-8", ADMIN_HTML);
}

void writeLe16(WiFiClient &client, uint16_t value) {
  uint8_t bytes[] = {static_cast<uint8_t>(value & 0xFF), static_cast<uint8_t>(value >> 8)};
  client.write(bytes, sizeof(bytes));
}

void writeLe32(WiFiClient &client, uint32_t value) {
  uint8_t bytes[] = {static_cast<uint8_t>(value & 0xFF),
                     static_cast<uint8_t>((value >> 8) & 0xFF),
                     static_cast<uint8_t>((value >> 16) & 0xFF),
                     static_cast<uint8_t>((value >> 24) & 0xFF)};
  client.write(bytes, sizeof(bytes));
}

void handleStartupBmp() {
  constexpr uint32_t rowBytes = STARTUP_LOGO_WIDTH * 3;
  constexpr uint32_t imageBytes = rowBytes * STARTUP_LOGO_HEIGHT;
  constexpr uint32_t fileBytes = 54 + imageBytes;
  WiFiClient client = adminServer.client();
  client.printf("HTTP/1.1 200 OK\r\n"
                "Content-Type: image/bmp\r\n"
                "Content-Length: %lu\r\n"
                "Cache-Control: public, max-age=86400\r\n"
                "Connection: close\r\n\r\n",
                static_cast<unsigned long>(fileBytes));
  client.write('B');
  client.write('M');
  writeLe32(client, fileBytes);
  writeLe16(client, 0);
  writeLe16(client, 0);
  writeLe32(client, 54);
  writeLe32(client, 40);
  writeLe32(client, STARTUP_LOGO_WIDTH);
  writeLe32(client, 0xFFFFFFFFUL - STARTUP_LOGO_HEIGHT + 1);  // Negative height: top-down BMP.
  writeLe16(client, 1);
  writeLe16(client, 24);
  writeLe32(client, 0);
  writeLe32(client, imageBytes);
  writeLe32(client, 2835);
  writeLe32(client, 2835);
  writeLe32(client, 0);
  writeLe32(client, 0);

  uint8_t row[rowBytes];
  for (int y = 0; y < STARTUP_LOGO_HEIGHT; ++y) {
    for (int x = 0; x < STARTUP_LOGO_WIDTH; ++x) {
      uint16_t color = pgm_read_word(&STARTUP_LOGO_BITMAP[y * STARTUP_LOGO_WIDTH + x]);
      uint8_t r = ((color >> 11) & 0x1F) * 255 / 31;
      uint8_t g = ((color >> 5) & 0x3F) * 255 / 63;
      uint8_t b = (color & 0x1F) * 255 / 31;
      row[x * 3 + 0] = b;
      row[x * 3 + 1] = g;
      row[x * 3 + 2] = r;
    }
    client.write(row, rowBytes);
  }
}

void handleApiStatus() {
  WeatherState value = demoMode ? demoWeather : weatherSnapshot();
  JsonDocument doc;
  doc["location"] = "朝来市";
  doc["mode"] = demoMode ? "demo" : "live";
  doc["sunrise"] = minuteText(value.sunriseMinutes);
  doc["sunset"] = minuteText(value.sunsetMinutes);
  doc["holiday"] = holidayToday;
  doc["moon"] = moonPhaseLabel(currentMoonPhase(true));
  uint32_t age = lastWeatherFetch == 0 ? 0 : (millis() - lastWeatherFetch) / 60000UL;
  doc["lastUpdate"] = lastWeatherFetch == 0 ? "等待刷新" : (String(age) + " 分钟前");

  JsonObject weatherJson = doc["weather"].to<JsonObject>();
  weatherJson["valid"] = value.valid;
  weatherJson["temp"] = isnan(value.temperature) ? 0.0F : value.temperature;
  weatherJson["feels"] = isnan(value.apparentTemperature) ? weatherJson["temp"].as<float>() : value.apparentTemperature;
  weatherJson["min"] = isnan(value.dailyMin) ? 0.0F : value.dailyMin;
  weatherJson["max"] = isnan(value.dailyMax) ? 0.0F : value.dailyMax;
  weatherJson["label"] = value.valid ? weatherLabel(value) : "天气获取中";
  weatherJson["code"] = value.code;
  weatherJson["cloud"] = value.cloudCover;
  weatherJson["wind"] = value.windSpeed;
  weatherJson["gust"] = value.windGust;
  weatherJson["rain"] = value.rain;
  weatherJson["snow"] = value.snowfall;
  JsonArray precip = weatherJson["precip"].to<JsonArray>();
  uint8_t count = value.precipitationProbabilityCount;
  if (count == 0) count = PRECIPITATION_HOURS;
  for (uint8_t i = 0; i < count; ++i) precip.add(value.precipitationProbability[i]);

  JsonArray forecast = doc["forecast"].to<JsonArray>();
  for (uint8_t i = 0; i < value.forecastCount; ++i) {
    WeatherState day;
    day.code = value.forecastCode[i];
    day.nextCode = value.forecastCode[i];
    JsonObject item = forecast.add<JsonObject>();
    item["label"] = weatherLabel(day);
    item["min"] = value.forecastMin[i];
    item["max"] = value.forecastMax[i];
    item["pop"] = value.forecastPrecipitation[i];
  }

  JsonObject settings = doc["settings"].to<JsonObject>();
  settings["outfitMode"] = adminSettings.outfitMode;
  settings["manualOutfit"] = adminSettings.manualOutfit;
  settings["brightness"] = adminSettings.brightness;
  settings["animationSpeed"] = adminSettings.animationSpeed;
  settings["riderSpeed"] = adminSettings.riderSpeed;
  settings["backgroundSpeed"] = adminSettings.backgroundSpeed;
  settings["dogSpeed"] = adminSettings.dogSpeed;
  settings["dogTint"] = adminSettings.dogTint;
  settings["dogNightBright"] = adminSettings.dogNightBright;
  settings["theme"] = adminSettings.theme;
  settings["showWifi"] = adminSettings.showWifi;
  settings["showSunSchedule"] = adminSettings.showSunSchedule;
  settings["showPrecipitation"] = adminSettings.showPrecipitation;
  settings["showFeelsLike"] = adminSettings.showFeelsLike;
  settings["showMoon"] = adminSettings.showMoon;
  settings["showHoliday"] = adminSettings.showHoliday;
  settings["showTemperature"] = adminSettings.showTemperature;
  settings["showDog"] = adminSettings.showDog;
  settings["showBirds"] = adminSettings.showBirds;
  settings["showWeatherEffects"] = adminSettings.showWeatherEffects;

  JsonObject wifi = doc["wifi"].to<JsonObject>();
  wifi["connected"] = WiFi.status() == WL_CONNECTED;
  wifi["ssid"] = safeJsonText(WiFi.SSID());
  wifi["ip"] = WiFi.localIP().toString();
  wifi["rssi"] = WiFi.RSSI();

  JsonArray saved = doc["savedWifi"].to<JsonArray>();
  Preferences preferences;
  if (preferences.begin("travelwifi", true)) {
    uint8_t savedCount = min(preferences.getUChar("count", 0), MAX_KNOWN_WIFI_NETWORKS);
    for (uint8_t index = 0; index < savedCount; ++index) {
      String ssid = preferences.getString(wifiPreferenceKey("ssid", index).c_str(), "");
      if (ssid.isEmpty()) continue;
      if (textHasControlBytes(ssid)) continue;
      JsonObject item = saved.add<JsonObject>();
      item["index"] = index;
      item["ssid"] = safeJsonText(ssid);
      item["current"] = WiFi.status() == WL_CONNECTED && ssid == WiFi.SSID();
    }
    preferences.end();
  }

  JsonObject device = doc["device"].to<JsonObject>();
  device["name"] = DEVICE_NAME;
  device["firmware"] = FIRMWARE_VERSION;
  device["web"] = WEB_UI_VERSION;
  device["heap"] = ESP.getFreeHeap();
  device["build"] = String(__DATE__) + " " + String(__TIME__);

  String output;
  serializeJson(doc, output);
  adminServer.sendHeader("Cache-Control", "no-store");
  adminServer.send(200, "application/json", output);
}

void handleApiSettings() {
  if (adminServer.hasArg("outfitMode")) adminSettings.outfitMode = constrain(adminServer.arg("outfitMode").toInt(), 0, 1);
  if (adminServer.hasArg("manualOutfit")) adminSettings.manualOutfit = constrain(adminServer.arg("manualOutfit").toInt(), 0, 5);
  if (adminServer.hasArg("brightness")) adminSettings.brightness = constrain(adminServer.arg("brightness").toInt(), 20, 100);
  if (adminServer.hasArg("animationSpeed")) adminSettings.animationSpeed = constrain(adminServer.arg("animationSpeed").toInt(), 60, 140);
  if (adminServer.hasArg("riderSpeed")) adminSettings.riderSpeed = constrain(adminServer.arg("riderSpeed").toInt(), 60, 140);
  if (adminServer.hasArg("backgroundSpeed")) adminSettings.backgroundSpeed = constrain(adminServer.arg("backgroundSpeed").toInt(), 60, 150);
  if (adminServer.hasArg("dogSpeed")) adminSettings.dogSpeed = constrain(adminServer.arg("dogSpeed").toInt(), 60, 160);
  if (adminServer.hasArg("dogTint")) adminSettings.dogTint = constrain(adminServer.arg("dogTint").toInt(), 0, 4);
  if (adminServer.hasArg("dogNightBright")) adminSettings.dogNightBright = constrain(adminServer.arg("dogNightBright").toInt(), 40, 100);
  if (adminServer.hasArg("theme")) adminSettings.theme = constrain(adminServer.arg("theme").toInt(), 0, 5);
  adminSettings.showWifi = argEnabled("showWifi", adminSettings.showWifi);
  adminSettings.showSunSchedule = argEnabled("showSunSchedule", adminSettings.showSunSchedule);
  adminSettings.showPrecipitation = argEnabled("showPrecipitation", adminSettings.showPrecipitation);
  adminSettings.showFeelsLike = argEnabled("showFeelsLike", adminSettings.showFeelsLike);
  adminSettings.showMoon = argEnabled("showMoon", adminSettings.showMoon);
  adminSettings.showHoliday = argEnabled("showHoliday", adminSettings.showHoliday);
  adminSettings.showTemperature = argEnabled("showTemperature", adminSettings.showTemperature);
  adminSettings.showDog = argEnabled("showDog", adminSettings.showDog);
  adminSettings.showBirds = argEnabled("showBirds", adminSettings.showBirds);
  adminSettings.showWeatherEffects = argEnabled("showWeatherEffects", adminSettings.showWeatherEffects);
  saveAdminSettings();
  applyAdminSettings();
  sendJsonOk();
}

void handleApiDemo() {
  activatePresetWeatherDemo(adminServer.arg("scene"));
  sendJsonOk();
}

void handleApiRefresh() {
  requestWeather = true;
  sendJsonOk();
}

void handleApiWifiAdd() {
  String ssid = adminServer.arg("ssid");
  String pass = adminServer.arg("pass");
  ssid.trim();
  if (!ssid.isEmpty()) {
    rememberNetwork(ssid, pass);
    loadKnownNetworks();
  }
  sendJsonOk();
}

void handleApiWifiDelete() {
  deleteKnownNetwork(constrain(adminServer.arg("index").toInt(), 0, MAX_KNOWN_WIFI_NETWORKS - 1));
  sendJsonOk();
}

void handleApiWifiClear() {
  clearKnownNetworks();
  sendJsonOk();
}

void handleApiReboot() {
  sendJsonOk();
  delay(250);
  ESP.restart();
}

void setupAdminServer() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (MDNS.begin("travel-clock")) {
    MDNS.addService("http", "tcp", 80);
    Serial.println("管理后台：http://travel-clock.local");
  }
  adminServer.on("/", HTTP_GET, handleAdminRoot);
  adminServer.on("/startup.bmp", HTTP_GET, handleStartupBmp);
  adminServer.on("/api/status", HTTP_GET, handleApiStatus);
  adminServer.on("/api/settings", HTTP_POST, handleApiSettings);
  adminServer.on("/api/demo", HTTP_POST, handleApiDemo);
  adminServer.on("/api/refresh", HTTP_POST, handleApiRefresh);
  adminServer.on("/api/wifi/add", HTTP_POST, handleApiWifiAdd);
  adminServer.on("/api/wifi/delete", HTTP_POST, handleApiWifiDelete);
  adminServer.on("/api/wifi/clear", HTTP_POST, handleApiWifiClear);
  adminServer.on("/api/reboot", HTTP_POST, handleApiReboot);
  adminServer.onNotFound([]() {
    adminServer.send(404, "application/json", "{\"ok\":false,\"error\":\"not_found\"}");
  });
  adminServer.begin();
  Serial.print("管理后台 IP：http://");
  Serial.println(WiFi.localIP());
}

void renderFrame() {
  WeatherState value = demoMode ? demoWeather : weatherSnapshot();
  float hour = demoMode ? demoHour : currentHour();
  bool resting = travelerResting(value, hour);
  updateSceneTarget(desiredScene(value, hour));
  paintBackground(value, hour, !resting);
  drawCelestialBody(value, hour);
  if (adminSettings.showBirds) {
    updateBirdFlock(value, hour);
    drawBirdFlock();
  } else {
    birdFlockActive = false;
  }
  if (resting) {
    drawCamp();
    if (adminSettings.showDog) drawSleepingBichon();
  } else {
    if (adminSettings.showDog) drawRunningBichon();
    drawRider(value);
  }
  if (adminSettings.showWeatherEffects) drawWeatherEffects(value);
  drawInterface(value);
  display.drawRGBBitmap(0, 0, frameBuffer.getBuffer(), SCENE_WIDTH, SCENE_HEIGHT);
  ++animationFrame;
  ++fpsFrames;
  uint32_t nowMs = millis();
  if (fpsWindowStart == 0) fpsWindowStart = nowMs;
  if (nowMs - fpsWindowStart >= 5000) {
    float fps = fpsFrames * 1000.0F / (nowMs - fpsWindowStart);
    Serial.printf("动画帧率：%.1f FPS，空闲内存：%u bytes\n", fps, ESP.getFreeHeap());
    fpsWindowStart = nowMs;
    fpsFrames = 0;
  }
}

void handleButton() {
  bool pressed = digitalRead(BOOT_BUTTON_PIN) == LOW;
  uint32_t now = millis();
  if (!pressed && singleClickPending && now - pendingClickAt > 320) {
    activateRandomWeatherDemo();
  }
  if (pressed && buttonDownAt == 0) {
    buttonDownAt = now;
    longPressHandled = false;
  }
  if (pressed && !longPressHandled && now - buttonDownAt >= 4000) {
    longPressHandled = true;
    singleClickPending = false;
    drawStatus("正在清除网络", "重启后请连接设置热点", WARM);
    wifiManager.resetSettings();
    clearKnownNetworks();
    delay(1200);
    ESP.restart();
  }
  if (!pressed && buttonDownAt != 0) {
    uint32_t duration = now - buttonDownAt;
    buttonDownAt = 0;
    if (!longPressHandled && duration > 40 && duration < 1200) {
      if (singleClickPending && now - pendingClickAt <= 360) {
        restoreLiveWeatherMode();
      } else {
        singleClickPending = true;
        pendingClickAt = now;
      }
    }
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  randomSeed(esp_random());

  // Keep both programmable board LEDs dark during normal operation. The RGB
  // NeoPixel has its own power gate; GPIO13 drives the separate user LED.
  pinMode(PIN_NEOPIXEL, OUTPUT);
  digitalWrite(PIN_NEOPIXEL, LOW);
  pinMode(NEOPIXEL_POWER, OUTPUT);
  digitalWrite(NEOPIXEL_POWER, !NEOPIXEL_POWER_ON);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
  pinMode(TFT_BACKLITE, OUTPUT);
  digitalWrite(TFT_BACKLITE, HIGH);
  pinMode(TFT_I2C_POWER, OUTPUT);
  digitalWrite(TFT_I2C_POWER, HIGH);
  delay(20);

  display.init(135, 240);
  display.setRotation(3);
  display.setTextWrap(false);
  display.fillScreen(BLACK);
  if (frameBuffer.getBuffer() == nullptr) {
    display.setCursor(8, 60);
    display.setTextColor(RED);
    display.print("Framebuffer error");
    while (true) delay(1000);
  }
  zh.begin(frameBuffer);
  zh.setFont(u8g2_font_wqy12_t_gb2312);
  zh.setFontMode(1);

  loadAdminSettings();
  applyAdminSettings();
  drawStartupLogo();
  WiFi.persistent(true);
  WiFi.setAutoReconnect(true);
  wifiManager.setAPCallback(onConfigPortal);
  wifiManager.setConfigPortalTimeout(300);
  wifiManager.setConnectTimeout(20);
  wifiManager.setHostname("travel-clock-v2");

  String legacySsid = wifiManager.getWiFiSSID(true);
  String legacyPassword = wifiManager.getWiFiPass(true);
  if (!legacySsid.isEmpty()) rememberNetwork(legacySsid, legacyPassword);
  else loadKnownNetworks();

  bool connected = connectKnownNetwork(18000);
  if (!connected) {
    wifiManager.autoConnect("TravelClock-Setup");
    if (WiFi.status() == WL_CONNECTED) {
      rememberNetwork(WiFi.SSID(), wifiManager.getWiFiPass(true));
    }
  }

  setupAdminServer();
  configTzTime(TIMEZONE, "ntp.nict.jp", "ntp.jst.mfeed.ad.jp", "pool.ntp.org");
  xTaskCreatePinnedToCore(weatherTask, "weather", 8192, nullptr, 1, nullptr, 0);
  nextFrameAt = micros();
}

void loop() {
  adminServer.handleClient();
  handleButton();
  uint32_t now = micros();
  if (static_cast<int32_t>(now - nextFrameAt) >= 0) {
    nextFrameAt += FRAME_US;
    renderFrame();
    if (static_cast<int32_t>(now - nextFrameAt) >= static_cast<int32_t>(FRAME_US * 2)) {
      nextFrameAt = now + FRAME_US;
    }
  } else {
    delay(1);
  }
}
