#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <SPI.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiManager.h>
#include <time.h>

#include "backgrounds.h"

namespace {

constexpr uint8_t BOOT_BUTTON_PIN = 0;
constexpr uint32_t WEATHER_REFRESH_MS = 10UL * 60UL * 1000UL;
constexpr uint32_t WEATHER_RETRY_MS = 30UL * 1000UL;
constexpr uint32_t WIFI_RESET_HOLD_MS = 4000;
constexpr uint32_t SCENE_FRAME_MS = 125;
constexpr float LATITUDE = 35.3233F;
constexpr float LONGITUDE = 134.8483F;
constexpr char LOCATION_NAME[] = "兵库县朝来市";
constexpr char TIMEZONE[] = "JST-9";

constexpr uint16_t BLACK = 0x0000;
constexpr uint16_t WHITE = 0xFFFF;
constexpr uint16_t MUTED = 0xBDF7;
constexpr uint16_t ACCENT = 0x5E7F;
constexpr uint16_t WARM = 0xFE26;
constexpr uint16_t GREEN = 0x57EA;
constexpr uint16_t RED = 0xF986;
constexpr uint16_t RAIN_BLUE = 0x7DFF;

Adafruit_ST7789 display(TFT_CS, TFT_DC, TFT_RST);
U8G2_FOR_ADAFRUIT_GFX zh;
WiFiManager wifiManager;

struct WeatherState {
  float temperature = NAN;
  float apparent = NAN;
  float humidity = NAN;
  float pressure = NAN;
  float wind = NAN;
  float precipitation = NAN;
  int code = -1;
  float dailyMin[3] = {NAN, NAN, NAN};
  float dailyMax[3] = {NAN, NAN, NAN};
  int dailyCode[3] = {-1, -1, -1};
  bool valid = false;
};

WeatherState weather;
uint8_t currentPage = 0;
uint32_t lastWeatherFetch = 0;
uint32_t lastWeatherAttempt = 0;
uint32_t lastRender = 0;
uint32_t buttonPressedAt = 0;
bool longPressHandled = false;

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

bool isRainCode(int code) {
  return (code >= 51 && code <= 67) || (code >= 80 && code <= 82) || code >= 95;
}

bool isSnowCode(int code) {
  return (code >= 71 && code <= 77) || (code >= 85 && code <= 86);
}

void drawChinese(const char *text, int x, int baseline, uint16_t color) {
  zh.setForegroundColor(color);
  zh.setCursor(x, baseline);
  zh.print(text);
}

bool localTime(struct tm &value) {
  return getLocalTime(&value, 10);
}

void drawCenteredChinese(const char *text, int centerX, int baseline, uint16_t color) {
  int width = zh.getUTF8Width(text);
  drawChinese(text, centerX - width / 2, baseline, color);
}

void drawStatus(const char *title, const char *message, uint16_t color) {
  display.fillScreen(0x0862);
  display.fillRoundRect(8, 25, 224, 82, 8, 0x10E4);
  drawChinese(title, 20, 53, color);
  drawChinese(message, 20, 82, WHITE);
}

void drawProvisioning() {
  display.fillScreen(0x0862);
  display.fillRoundRect(7, 7, 226, 121, 8, 0x10E4);
  drawChinese("旅行天气时钟", 17, 28, ACCENT);
  drawChinese("手机连接热点：", 17, 51, WHITE);
  display.setTextColor(WARM);
  display.setTextSize(2);
  display.setCursor(17, 61);
  display.print("TravelClock-Setup");
  drawChinese("浏览器打开 192.168.4.1", 17, 99, MUTED);
  drawChinese("选择家中 Wi-Fi 并保存", 17, 119, MUTED);
}

void onConfigPortal(WiFiManager *) {
  drawProvisioning();
}

bool fetchWeather() {
  if (WiFi.status() != WL_CONNECTED) return false;
  lastWeatherAttempt = millis();

  char url[700];
  snprintf(url, sizeof(url),
           "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
           "&current=temperature_2m,relative_humidity_2m,apparent_temperature,"
           "precipitation,weather_code,surface_pressure,wind_speed_10m"
           "&daily=weather_code,temperature_2m_max,temperature_2m_min"
           "&forecast_days=3&temperature_unit=celsius&wind_speed_unit=kmh"
           "&timezone=Asia%%2FTokyo",
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

  JsonObject current = doc["current"];
  weather.temperature = current["temperature_2m"].as<float>();
  weather.apparent = current["apparent_temperature"].as<float>();
  weather.humidity = current["relative_humidity_2m"].as<float>();
  weather.pressure = current["surface_pressure"].as<float>();
  weather.wind = current["wind_speed_10m"].as<float>();
  weather.precipitation = current["precipitation"].as<float>();
  weather.code = current["weather_code"].as<int>();

  for (int i = 0; i < 3; ++i) {
    weather.dailyMin[i] = doc["daily"]["temperature_2m_min"][i].as<float>();
    weather.dailyMax[i] = doc["daily"]["temperature_2m_max"][i].as<float>();
    weather.dailyCode[i] = doc["daily"]["weather_code"][i].as<int>();
  }

  weather.valid = true;
  lastWeatherFetch = millis();
  Serial.printf("朝来市：%.1f°C，%s，湿度 %.0f%%\n",
                weather.temperature, weatherText(weather.code), weather.humidity);
  return true;
}

const uint16_t *selectBackground(const struct tm &now) {
  if (weather.valid && isSnowCode(weather.code)) return BACKGROUND_SNOW;
  if (weather.valid && isRainCode(weather.code)) return BACKGROUND_RAIN;
  if (now.tm_hour < 6 || now.tm_hour >= 18) return BACKGROUND_NIGHT;
  return BACKGROUND_DAY;
}

void drawWeatherParticles(uint32_t frame) {
  if (!weather.valid) return;
  if (isRainCode(weather.code)) {
    for (int i = 0; i < 20; ++i) {
      int x = (i * 37 + frame * 7) % 248 - 4;
      int y = 22 + ((i * 23 + frame * 5) % 73);
      display.drawLine(x, y, x - 2, y + 5, RAIN_BLUE);
    }
  } else if (isSnowCode(weather.code)) {
    for (int i = 0; i < 18; ++i) {
      int x = (i * 43 + frame * (1 + i % 2)) % 240;
      int y = 20 + ((i * 19 + frame * 2) % 78);
      display.fillCircle(x, y, 1 + (i % 3 == 0), WHITE);
    }
  }
}

void drawTraveler(int x, int y, uint32_t frame) {
  int bob = (frame & 1) ? 0 : 1;
  uint16_t outline = 0x18C3;
  uint16_t skin = 0xFD8E;
  uint16_t jacket = 0xE924;
  uint16_t pack = 0xFE20;

  display.drawCircle(x + 4, y + 13, 4, outline);
  display.drawCircle(x + 17, y + 13, 4, outline);
  display.drawLine(x + 4, y + 13, x + 10, y + 8, outline);
  display.drawLine(x + 10, y + 8, x + 17, y + 13, outline);
  display.drawLine(x + 4, y + 13, x + 12, y + 13, outline);
  display.drawLine(x + 12, y + 13, x + 10, y + 8, outline);
  if (frame & 1) {
    display.drawLine(x + 4, y + 9, x + 4, y + 17, MUTED);
    display.drawLine(x + 17, y + 9, x + 17, y + 17, MUTED);
  } else {
    display.drawLine(x, y + 13, x + 8, y + 13, MUTED);
    display.drawLine(x + 13, y + 13, x + 21, y + 13, MUTED);
  }

  display.fillRect(x + 9, y + 3 + bob, 5, 6, jacket);
  display.fillRect(x + 7, y + 4 + bob, 3, 5, pack);
  display.fillCircle(x + 13, y + 1 + bob, 3, skin);
  display.fillRect(x + 10, y - 2 + bob, 7, 2, WARM);
  display.drawLine(x + 11, y + 8 + bob, x + 7, y + 12, outline);
  display.drawLine(x + 13, y + 8 + bob, x + 16, y + 11, outline);
}

void drawScenePage() {
  struct tm now{};
  bool ready = localTime(now);
  const uint16_t *background = ready ? selectBackground(now) : BACKGROUND_DAY;
  display.drawRGBBitmap(0, 0, background, BACKGROUND_WIDTH, BACKGROUND_HEIGHT);

  uint32_t frame = millis() / SCENE_FRAME_MS;
  drawWeatherParticles(frame);
  int travelerX = static_cast<int>((millis() / 55) % 280) - 24;
  drawTraveler(travelerX, 77, frame);

  display.fillRect(0, 0, 240, 22, BLACK);
  display.fillRect(0, 100, 240, 35, BLACK);
  display.drawFastHLine(0, 100, 240, 0x39E7);

  char timeText[8] = "--:--";
  char dateText[32] = "同步时间";
  if (ready) {
    snprintf(timeText, sizeof(timeText), "%02d:%02d", now.tm_hour, now.tm_min);
    const char *week[] = {"日", "一", "二", "三", "四", "五", "六"};
    snprintf(dateText, sizeof(dateText), "%02d月%02d日 周%s",
             now.tm_mon + 1, now.tm_mday, week[now.tm_wday]);
  }

  display.setTextColor(WHITE);
  display.setTextSize(2);
  display.setCursor(4, 4);
  display.print(timeText);
  drawChinese(dateText, 71, 16, WHITE);

  if (weather.valid) {
    char range[32];
    snprintf(range, sizeof(range), "%.0f~%.0fC", weather.dailyMin[0], weather.dailyMax[0]);
    display.setTextColor(ACCENT);
    display.setTextSize(1);
    display.setCursor(183, 4);
    display.print(range);
    drawChinese(weatherText(weather.code), 201, 18, ACCENT);
  }

  drawChinese(LOCATION_NAME, 5, 121, WHITE);
  display.fillCircle(10, 130, 2, WiFi.status() == WL_CONNECTED ? GREEN : RED);
  if (weather.valid) {
    char values[64];
    snprintf(values, sizeof(values), "%.1fC    %.0f%%", weather.temperature, weather.humidity);
    display.setTextColor(WARM);
    display.setTextSize(2);
    display.setCursor(112, 111);
    display.print(values);
    display.drawCircle(148, 111, 2, WARM);
  } else {
    drawChinese("天气连接中", 156, 122, MUTED);
  }
}

void drawForecastPage() {
  display.fillScreen(0x0862);
  drawChinese("朝来市 · 未来三天", 10, 20, ACCENT);
  display.drawFastHLine(8, 27, 224, 0x39E7);

  const char *labels[3] = {"今天", "明天", "后天"};
  for (int i = 0; i < 3; ++i) {
    int x = 7 + i * 78;
    display.fillRoundRect(x, 36, 70, 87, 7, 0x10E4);
    drawCenteredChinese(labels[i], x + 35, 55, i == 0 ? WARM : WHITE);
    if (weather.valid) {
      drawCenteredChinese(weatherText(weather.dailyCode[i]), x + 35, 78, ACCENT);
      char range[32];
      snprintf(range, sizeof(range), "%.0f / %.0f", weather.dailyMax[i], weather.dailyMin[i]);
      display.setTextColor(WHITE);
      display.setTextSize(1);
      display.setCursor(x + 10, 91);
      display.print(range);
      drawChinese("最高 / 最低", x + 8, 114, MUTED);
    } else {
      drawCenteredChinese("等待数据", x + 35, 84, MUTED);
    }
  }
}

void drawDigitalClockPage() {
  display.fillScreen(BLACK);
  struct tm now{};
  bool ready = localTime(now);
  char timeText[12] = "--:--:--";
  char dateText[48] = "正在同步日本时间";
  if (ready) {
    snprintf(timeText, sizeof(timeText), "%02d:%02d:%02d",
             now.tm_hour, now.tm_min, now.tm_sec);
    const char *week[] = {"日", "一", "二", "三", "四", "五", "六"};
    snprintf(dateText, sizeof(dateText), "%d年%d月%d日  周%s",
             now.tm_year + 1900, now.tm_mon + 1, now.tm_mday, week[now.tm_wday]);
  }

  display.setTextColor(WHITE);
  display.setTextSize(4);
  display.setCursor(23, 27);
  display.print(timeText);
  drawCenteredChinese(dateText, 120, 85, ACCENT);
  if (weather.valid) {
    char weatherLine[64];
    snprintf(weatherLine, sizeof(weatherLine), "朝来市  %s  %.1f C  湿度 %.0f%%",
             weatherText(weather.code), weather.temperature, weather.humidity);
    drawCenteredChinese(weatherLine, 120, 116, WARM);
  }
  drawCenteredChinese("短按 BOOT 切换页面", 120, 132, MUTED);
}

void renderCurrentPage() {
  if (currentPage == 0) drawScenePage();
  else if (currentPage == 1) drawForecastPage();
  else drawDigitalClockPage();
}

void handleButton() {
  bool pressed = digitalRead(BOOT_BUTTON_PIN) == LOW;
  if (pressed && buttonPressedAt == 0) {
    buttonPressedAt = millis();
    longPressHandled = false;
  }

  if (pressed && !longPressHandled &&
      millis() - buttonPressedAt >= WIFI_RESET_HOLD_MS) {
    longPressHandled = true;
    drawStatus("正在清除网络", "重启后请重新连接设置热点", WARM);
    wifiManager.resetSettings();
    delay(1500);
    ESP.restart();
  }

  if (!pressed && buttonPressedAt != 0) {
    uint32_t duration = millis() - buttonPressedAt;
    buttonPressedAt = 0;
    if (!longPressHandled && duration > 40 && duration < 1200) {
      currentPage = (currentPage + 1) % 3;
      renderCurrentPage();
    }
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
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
  zh.begin(display);
  zh.setFont(u8g2_font_wqy12_t_gb2312);
  zh.setFontMode(1);

  drawStatus("旅行天气时钟", "正在连接 Wi-Fi…", ACCENT);
  wifiManager.setAPCallback(onConfigPortal);
  wifiManager.setConfigPortalTimeout(300);
  wifiManager.setConnectTimeout(20);
  wifiManager.setHostname("travel-weather-clock");
  wifiManager.autoConnect("TravelClock-Setup");

  configTzTime(TIMEZONE, "ntp.nict.jp", "ntp.jst.mfeed.ad.jp", "pool.ntp.org");
  drawStatus("网络已连接", "正在获取朝来市天气…", GREEN);
  fetchWeather();
  renderCurrentPage();
}

void loop() {
  handleButton();

  bool initialRetryDue = !weather.valid &&
                         millis() - lastWeatherAttempt >= WEATHER_RETRY_MS;
  bool refreshDue = weather.valid &&
                    millis() - lastWeatherFetch >= WEATHER_REFRESH_MS;
  if (WiFi.status() == WL_CONNECTED && (initialRetryDue || refreshDue)) {
    fetchWeather();
  }

  uint32_t interval = currentPage == 0 ? SCENE_FRAME_MS : 1000;
  if (millis() - lastRender >= interval) {
    lastRender = millis();
    renderCurrentPage();
  }
  delay(5);
}
