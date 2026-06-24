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

namespace {

constexpr int SCREEN_WIDTH = 240;
constexpr int SCREEN_HEIGHT = 135;
constexpr uint32_t WEATHER_REFRESH_MS = 10UL * 60UL * 1000UL;
constexpr uint32_t WEATHER_RETRY_MS = 30UL * 1000UL;
constexpr uint32_t WIFI_RESET_HOLD_MS = 3000;
constexpr uint8_t BOOT_BUTTON_PIN = 0;

// Asago, Hyogo, Japan (near the Wadayama weather observation area).
constexpr float LATITUDE = 35.3233F;
constexpr float LONGITUDE = 134.8483F;
constexpr char LOCATION_NAME[] = "兵库县朝来市";
constexpr char TIMEZONE[] = "JST-9";

constexpr uint16_t COLOR_BG = 0x0862;
constexpr uint16_t COLOR_PANEL = 0x10E4;
constexpr uint16_t COLOR_ACCENT = 0x4E7F;
constexpr uint16_t COLOR_WARM = 0xFDC6;
constexpr uint16_t COLOR_MUTED = 0x9CF3;
constexpr uint16_t COLOR_WHITE = 0xFFFF;
constexpr uint16_t COLOR_GREEN = 0x57EA;
constexpr uint16_t COLOR_RED = 0xF986;

Adafruit_ST7789 display(TFT_CS, TFT_DC, TFT_RST);
GFXcanvas16 canvas(SCREEN_WIDTH, SCREEN_HEIGHT);
U8G2_FOR_ADAFRUIT_GFX zh;
WiFiManager wifiManager;

struct WeatherState {
  float temperature = NAN;
  float apparentTemperature = NAN;
  float humidity = NAN;
  float pressure = NAN;
  float windSpeed = NAN;
  float precipitation = NAN;
  int weatherCode = -1;
  bool valid = false;
};

WeatherState weather;
uint32_t lastWeatherFetch = 0;
uint32_t lastWeatherAttempt = 0;
uint32_t lastFrame = 0;
uint32_t bootPressedAt = 0;

const char *weatherText(int code) {
  if (code == 0) return "晴朗";
  if (code <= 2) return "晴间多云";
  if (code == 3) return "阴天";
  if (code == 45 || code == 48) return "有雾";
  if (code >= 51 && code <= 57) return "毛毛雨";
  if (code >= 61 && code <= 67) return "下雨";
  if (code >= 71 && code <= 77) return "下雪";
  if (code >= 80 && code <= 82) return "阵雨";
  if (code >= 85 && code <= 86) return "阵雪";
  if (code >= 95) return "雷雨";
  return "天气未知";
}

void drawChinese(const char *text, int x, int baseline, uint16_t color) {
  zh.setForegroundColor(color);
  zh.setCursor(x, baseline);
  zh.print(text);
}

void pushFrame() {
  display.drawRGBBitmap(0, 0, canvas.getBuffer(), SCREEN_WIDTH, SCREEN_HEIGHT);
}

void drawProvisioning() {
  canvas.fillScreen(COLOR_BG);
  canvas.fillRoundRect(8, 8, 224, 119, 8, COLOR_PANEL);
  drawChinese("首次连接", 18, 29, COLOR_ACCENT);
  drawChinese("请连接 Wi-Fi 热点：", 18, 53, COLOR_WHITE);

  canvas.setTextColor(COLOR_WARM);
  canvas.setTextSize(2);
  canvas.setCursor(18, 63);
  canvas.print("WeatherClock-Setup");

  drawChinese("然后打开 192.168.4.1", 18, 101, COLOR_MUTED);
  drawChinese("选择家中网络并保存", 18, 119, COLOR_MUTED);
  pushFrame();
}

void drawStatus(const char *title, const char *message, uint16_t color) {
  canvas.fillScreen(COLOR_BG);
  canvas.fillRoundRect(10, 28, 220, 78, 8, COLOR_PANEL);
  drawChinese(title, 22, 55, color);
  drawChinese(message, 22, 83, COLOR_WHITE);
  pushFrame();
}

void onConfigPortal(WiFiManager *) {
  drawProvisioning();
}

bool fetchWeather() {
  if (WiFi.status() != WL_CONNECTED) return false;
  lastWeatherAttempt = millis();

  char url[512];
  snprintf(url, sizeof(url),
           "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
           "&current=temperature_2m,relative_humidity_2m,apparent_temperature,"
           "precipitation,weather_code,cloud_cover,surface_pressure,wind_speed_10m"
           "&temperature_unit=celsius&wind_speed_unit=kmh&timezone=Asia%%2FTokyo",
           LATITUDE, LONGITUDE);

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(12000);

  if (!http.begin(client, url)) return false;
  int status = http.GET();
  if (status != HTTP_CODE_OK) {
    Serial.printf("天气请求失败，HTTP %d\n", status);
    http.end();
    return false;
  }

  // Reading the complete body first handles HTTPS chunked responses reliably
  // across Arduino-ESP32 core versions.
  String payload = http.getString();
  JsonDocument document;
  DeserializationError error = deserializeJson(document, payload);
  if (error) {
    Serial.printf("天气 JSON 解析失败：%s\n", error.c_str());
    http.end();
    return false;
  }

  JsonObject current = document["current"];
  weather.temperature = current["temperature_2m"].as<float>();
  weather.apparentTemperature = current["apparent_temperature"].as<float>();
  weather.humidity = current["relative_humidity_2m"].as<float>();
  weather.pressure = current["surface_pressure"].as<float>();
  weather.windSpeed = current["wind_speed_10m"].as<float>();
  weather.precipitation = current["precipitation"].as<float>();
  weather.weatherCode = current["weather_code"].as<int>();
  weather.valid = true;
  lastWeatherFetch = millis();
  http.end();

  Serial.printf("朝来市天气：%.1f°C，湿度 %.0f%%，风速 %.1f km/h\n",
                weather.temperature, weather.humidity, weather.windSpeed);
  return true;
}

void drawClock() {
  canvas.fillScreen(COLOR_BG);
  canvas.fillRoundRect(5, 5, 230, 125, 8, COLOR_PANEL);

  drawChinese(LOCATION_NAME, 13, 20, COLOR_ACCENT);
  canvas.fillCircle(221, 14, 4,
                    WiFi.status() == WL_CONNECTED ? COLOR_GREEN : COLOR_RED);

  struct tm now;
  bool timeReady = getLocalTime(&now, 10);
  char timeText[8] = "--:--";
  char dateText[48] = "正在同步日本时间";
  if (timeReady) {
    snprintf(timeText, sizeof(timeText), "%02d:%02d", now.tm_hour, now.tm_min);
    const char *weekdays[] = {"日", "一", "二", "三", "四", "五", "六"};
    snprintf(dateText, sizeof(dateText), "%d月%d日  周%s",
             now.tm_mon + 1, now.tm_mday, weekdays[now.tm_wday]);
  }

  canvas.setTextColor(COLOR_WHITE);
  canvas.setTextSize(4);
  canvas.setCursor(12, 32);
  canvas.print(timeText);
  drawChinese(dateText, 13, 82, COLOR_MUTED);

  canvas.drawFastVLine(143, 29, 70, 0x31A6);
  if (weather.valid) {
    char temperatureText[16];
    snprintf(temperatureText, sizeof(temperatureText), "%.1f C", weather.temperature);
    canvas.setTextColor(COLOR_WARM);
    canvas.setTextSize(2);
    canvas.setCursor(153, 35);
    canvas.print(temperatureText);
    canvas.drawCircle(198, 36, 2, COLOR_WARM);
    drawChinese(weatherText(weather.weatherCode), 153, 73, COLOR_WHITE);

    char feelsLike[24];
    snprintf(feelsLike, sizeof(feelsLike), "体感 %.0f C", weather.apparentTemperature);
    drawChinese(feelsLike, 153, 94, COLOR_MUTED);
  } else {
    drawChinese("等待天气", 153, 55, COLOR_WARM);
    drawChinese("正在联网", 153, 79, COLOR_MUTED);
  }

  canvas.fillRoundRect(10, 105, 220, 21, 5, 0x18E5);
  if (weather.valid) {
    char details[96];
    snprintf(details, sizeof(details), "湿度 %.0f%%   风 %.0f km/h   降水 %.1f mm",
             weather.humidity, weather.windSpeed, weather.precipitation);
    drawChinese(details, 16, 121, COLOR_WHITE);
  } else {
    drawChinese("天气数据准备中 · 数据源 Open-Meteo", 16, 121, COLOR_MUTED);
  }

  pushFrame();
}

void handleWiFiReset() {
  if (digitalRead(BOOT_BUTTON_PIN) == LOW) {
    if (bootPressedAt == 0) bootPressedAt = millis();
    if (millis() - bootPressedAt >= WIFI_RESET_HOLD_MS) {
      drawStatus("正在清除网络", "稍后请重新连接设置热点", COLOR_WARM);
      wifiManager.resetSettings();
      delay(1500);
      ESP.restart();
    }
  } else {
    bootPressedAt = 0;
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
  display.fillScreen(ST77XX_BLACK);
  zh.begin(canvas);
  zh.setFont(u8g2_font_wqy12_t_gb2312);
  zh.setFontMode(1);

  drawStatus("中文天气时钟", "正在连接 Wi-Fi…", COLOR_ACCENT);
  wifiManager.setAPCallback(onConfigPortal);
  wifiManager.setConfigPortalTimeout(300);
  wifiManager.setConnectTimeout(20);
  wifiManager.setHostname("weather-clock-cn");

  if (!wifiManager.autoConnect("WeatherClock-Setup")) {
    drawStatus("网络尚未配置", "重启后可再次进入配网页面", COLOR_WARM);
    delay(2500);
  }

  configTzTime(TIMEZONE, "ntp.nict.jp", "ntp.jst.mfeed.ad.jp", "pool.ntp.org");
  drawStatus("网络已连接", "正在获取朝来市天气…", COLOR_GREEN);
  fetchWeather();
  drawClock();
}

void loop() {
  handleWiFiReset();

  bool initialRetryDue = !weather.valid &&
                         millis() - lastWeatherAttempt >= WEATHER_RETRY_MS;
  bool refreshDue = weather.valid &&
                    millis() - lastWeatherFetch >= WEATHER_REFRESH_MS;
  if (WiFi.status() == WL_CONNECTED && (initialRetryDue || refreshDue)) {
    fetchWeather();
  }

  if (millis() - lastFrame >= 1000) {
    lastFrame = millis();
    drawClock();
  }

  delay(10);
}
