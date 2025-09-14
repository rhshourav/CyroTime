#include <WiFi.h>
#include <WebServer.h>
#include <EEPROM.h>
#include <PubSubClient.h>
#include <MD_Parola.h>
#include <MD_MAX72XX.h>
#include <SPI.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>



// ========================== PINS & MATRIX ==========================
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 4
#define DATA_PIN 23
#define CLK_PIN  18
#define CS_PIN   5
#define IR_PIN 25
#define LIGHT_PIN 27
#define LDR_PIN 34
#define C_IN 16
#define RESET_BTN 15
#define BAT_CHARGE 35
#define R1 9860.0
#define R2 2165.0

MD_Parola display = MD_Parola(HARDWARE_TYPE, DATA_PIN, CLK_PIN, CS_PIN, MAX_DEVICES);
// === Calibration ===
// If your multimeter shows 4.124 V and ESP reads 3.36 V,
// correction factor = 4.124 / 3.36 = ~1.227
const float CORRECTION_FACTOR = 1.227;

// ========================== OLED SETTINGS =========================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1  // Reset pin # (or -1 if sharing Arduino reset)
Adafruit_SSD1306 oled(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);


// ========================== EEPROM SETTINGS ========================
#define EEPROM_SIZE 512

struct Config {
  char ssid[32];
  char pass[32];
  char mqtt[32];
  char mqtt_user[32];
  char mqtt_pass[32];
  int mqtt_port;
};

Config config;

// ========================== GLOBALS ================================
WiFiClient espClient;
PubSubClient mqttClient(espClient);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "time.google.com", 3600 * 6, 60000);
WebServer server(80);

bool lightState = false;
bool showColon = true;
bool lastIRState = false;
unsigned long lastIRToggle = 0;
unsigned long lastUpdate = 0;

// ========================== HTML CONFIG PAGE ========================
const char* configPage = R"rawliteral(
<!DOCTYPE html>
<html><head>
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>ESP32 Config</title>
  <style>
    :root {
      --bg: #f7f9fc;
      --text: #333;
      --card: #fff;
      --btn: #007bff;
      --btn-hover: #0056b3;
    }
    body.dark {
      --bg: #1e1e1e;
      --text: #eee;
      --card: #2c2c2c;
      --btn: #4e8cff;
      --btn-hover: #3972d1;
    }
    body {
      background: var(--bg); color: var(--text);
      font-family: sans-serif; padding: 20px;
    }
    h2 { text-align: center; }
    form {
      max-width: 400px; margin: auto;
      background: var(--card); padding: 20px;
      border-radius: 10px;
    }
    input {
      width: 100%; padding: 10px; margin: 10px 0;
      border-radius: 5px; border: 1px solid #ccc;
    }
    input[type=submit] {
      background: var(--btn); color: #fff; border: none;
    }
    input[type=submit]:hover { background: var(--btn-hover); }
    .toggle-container {
      text-align: center; margin: 20px 0;
    }
    .slider {
      position: relative; display: inline-block;
      width: 50px; height: 26px;
    }
    .slider input { opacity: 0; width: 0; height: 0; }
    .slider span {
      position: absolute; cursor: pointer;
      top: 0; left: 0; right: 0; bottom: 0;
      background: #ccc; transition: .4s;
      border-radius: 30px;
    }
    .slider span:before {
      content: ""; position: absolute;
      height: 20px; width: 20px; left: 3px; bottom: 3px;
      background: white; transition: .4s; border-radius: 50%;
    }
    input:checked + span { background: #4e8cff; }
    input:checked + span:before { transform: translateX(24px); }
  </style>
  <script>
    window.onload = function() {
      const dark = localStorage.getItem("darkMode");
      if (dark === "enabled") {
        document.body.classList.add("dark");
        document.getElementById("themeToggle").checked = true;
      }
    }
    function toggleDark(el) {
      if (el.checked) {
        document.body.classList.add("dark");
        localStorage.setItem("darkMode", "enabled");
      } else {
        document.body.classList.remove("dark");
        localStorage.setItem("darkMode", "disabled");
      }
    }
  </script>
</head>
<body>
  <h2>ESP32 Setup</h2>
  <div class="toggle-container">
    <label class="slider">
      <input type="checkbox" id="themeToggle" onchange="toggleDark(this)">
      <span></span>
    </label>
  </div>
  <form method="POST" action="/save">
    <input name="ssid" placeholder="WiFi SSID" required>
    <input name="pass" type="password" placeholder="WiFi Password" required>
    <input name="mqtt" placeholder="MQTT Server IP" required>
    <input name="port" type="number" value="1883" required>
    <input name="mqtt_user" placeholder="MQTT Username">
    <input name="mqtt_pass" type="password" placeholder="MQTT Password">
    <input type="submit" value="Save & Reboot">
  </form>
</body></html>
)rawliteral";

// ========================== EEPROM ================================
void saveConfig() {
  EEPROM.put(0, config);
  EEPROM.commit();
}

void loadConfig() {
  EEPROM.get(0, config);
}

// ========================== WIFI & WEB ==============================
void startAPMode() {
  WiFi.softAP("CyroTime", "12345678");
  server.on("/", []() { server.send(200, "text/html", configPage); });
  server.on("/save", HTTP_POST, []() {
    strncpy(config.ssid, server.arg("ssid").c_str(), 32);
    strncpy(config.pass, server.arg("pass").c_str(), 32);
    strncpy(config.mqtt, server.arg("mqtt").c_str(), 32);
    strncpy(config.mqtt_user, server.arg("mqtt_user").c_str(), 32);
    strncpy(config.mqtt_pass, server.arg("mqtt_pass").c_str(), 32);
    config.mqtt_port = server.arg("port").toInt();
    saveConfig();
    server.send(200, "text/html", "<h3>Saved! Rebooting...</h3>");
    delay(2000);
    ESP.restart();
  });
  server.begin();
}

// ========================== MQTT ===============================
void mqttCallback(char* topic, byte* payload, unsigned int len) {
  String msg;
  for (unsigned int i = 0; i < len; i++) msg += (char)payload[i];
  if (String(topic) == "home/Light/control") {
    lightState = (msg == "ON");
    digitalWrite(LIGHT_PIN, lightState ? HIGH : LOW);
    mqttClient.publish("home/Light/state", lightState ? "ON" : "OFF", true);
  }
}

void connectMQTT() {
  oled.clearDisplay();
  oled.setCursor(0, 0);
  oled.setTextSize(1);
  oled.println("Connecting MQTT...");
  oled.println(config.mqtt);
  oled.display();

  if (mqttClient.connected()) return;
  mqttClient.setServer(config.mqtt, config.mqtt_port);
  mqttClient.setCallback(mqttCallback);
  if (mqttClient.connect("ESP32", config.mqtt_user, config.mqtt_pass)) {
    oled.clearDisplay();
    oled.setTextSize(1);
    oled.setCursor(0, 0);
    oled.println("MQTT Connected!");
    oled.println("Sub: home/Light");
    oled.display();

    mqttClient.subscribe("home/Light/control");
    mqttClient.publish("home/Light/state", lightState ? "ON" : "OFF", true);
    mqttClient.publish("homeassistant/light/Light/config",
      "{\"name\":\"Light\",\"unique_id\":\"esp32_light_1\",\"state_topic\":\"home/Light/state\",\"command_topic\":\"home/Light/control\",\"payload_on\":\"ON\",\"payload_off\":\"OFF\",\"retain\":true}", true);
  }else {
    oled.clearDisplay();
    oled.setCursor(0, 0);
    oled.println("MQTT Failed.");
    oled.display();

  }
}

// ========================== TASKS ===============================
void IR_MQTT_Task(void* p) {
  for (;;) {
    bool irDetected = digitalRead(IR_PIN) == LOW;
    if (irDetected && !lastIRState && millis() - lastIRToggle > 500) {
      lightState = !lightState;
      digitalWrite(LIGHT_PIN, lightState ? HIGH : LOW);
      mqttClient.publish("home/Light/state", lightState ? "ON" : "OFF", true);
      lastIRToggle = millis();
    }
    lastIRState = irDetected;

    if (WiFi.status() == WL_CONNECTED) {
      mqttClient.loop();
    }
    delay(10);
  }
}

void Display_Task(void* p) {
  for (;;) {
    int lightVal = analogRead(LDR_PIN);
    int brightness = map(lightVal, 0, 4095, 1, 15);
    display.setIntensity(brightness);

    if (millis() - lastUpdate > 1000) {
      lastUpdate = millis();
      timeClient.update();
      int h24 = timeClient.getHours();
      int h12 = h24 % 12;
      if (h12 == 0) h12 = 12;  // Convert 0 to 12 for 12-hour format
      int m = timeClient.getMinutes();

      char timeStr[6];
      snprintf(timeStr, sizeof(timeStr), "%2d%c%02d", h12, showColon ? ':' : ' ', m);

      showColon = !showColon;

      display.displayClear();
      display.displayText(timeStr, PA_CENTER, 100, 0, PA_PRINT, PA_NO_EFFECT);
    }
    display.displayAnimate();
    delay(10);
  }
}
void drawBatteryIcon(float voltage, bool charging) {
  const int x = 100;  // Top-right corner (adjust for your screen width)
  const int y = 0;
  const int w = 24;
  const int h = 10;

  // Battery outline
  oled.drawRect(x, y, w, h, WHITE);
  oled.fillRect(x + w, y + 3, 2, 4, WHITE);  // Battery tip

  // Estimate battery level
  int level = 0;
  if (voltage >= 4.2) level = 100;
  else if (voltage >= 4.0) level = 80;
  else if (voltage >= 3.8) level = 60;
  else if (voltage >= 3.6) level = 40;
  else if (voltage >= 3.4) level = 20;
  else level = 5;

  // Fill battery
  int fillWidth = map(level, 0, 100, 0, w - 4);
  oled.fillRect(x + 2, y + 2, fillWidth, h - 4, WHITE);

  // Optional charging indicator
  if (charging) {
    oled.setCursor(x + 6, y - 1);
    oled.setTextSize(1);
    oled.print("*");  // Simple charging symbol
  }
}

void OLED_Status_Task(void* p) {
  const int STATUS_COUNT = 5;
  int statusIndex = 0;

  for (;;) {
    oled.clearDisplay();

    // --- Read battery voltage and charging state ---
    int rawADC = analogRead(BAT_CHARGE);
    float vOut = (rawADC / 4095.0) * 3.3;
    float vIn = vOut * ((R1 + R2) / R2) * CORRECTION_FACTOR;
    bool charging = digitalRead(C_IN);

    // --- Draw battery icon at top ---
    drawBatteryIcon(vIn, charging);

    // --- Get time and format as 12-hour ---
    time_t rawTime = timeClient.getEpochTime();
    struct tm* timeInfo = localtime(&rawTime);

    int hour = timeInfo->tm_hour;
    int minute = timeInfo->tm_min;
    bool isPM = false;

    if (hour >= 12) {
      isPM = true;
      if (hour > 12) hour -= 12;
    } else if (hour == 0) {
      hour = 12;
    }

    char timeString[10];
    sprintf(timeString, "%02d:%02d %s", hour, minute, isPM ? "PM" : "AM");

    // --- Draw clock below battery icon ---
    oled.setTextSize(2);  // Large clock
    int16_t x1, y1;
    uint16_t w, h;
    oled.getTextBounds(timeString, 0, 0, &x1, &y1, &w, &h);

    // Place clock below battery icon (around y = 12~15)
    oled.setCursor((oled.width() - w) / 2, 14);
    oled.println(timeString);

    // --- Draw one status line below the clock ---
    oled.setTextSize(1);
    oled.setCursor(0, 38);  // Adjust as needed

    switch (statusIndex) {
      case 0:
        oled.print("WiFi: ");
        oled.println(WiFi.status() == WL_CONNECTED ? "Normal" : "Disconnected");
        break;

      case 1:
        oled.print("MQTT: ");
        oled.println(mqttClient.connected() ? "Connected" : "Disconnected");
        break;

      case 2:
        oled.print("Charging: ");
        oled.println(charging ? "Yes" : "No");
        break;

      case 3:
        oled.print("Battery: ");
        oled.print(vIn, 2);
        oled.println(" V");
        break;

      case 4:
        oled.print("IP: ");
        oled.println(WiFi.localIP());
        break;
    }

    oled.display();

    // Rotate status every 3 seconds
    statusIndex = (statusIndex + 1) % STATUS_COUNT;
    vTaskDelay(pdMS_TO_TICKS(4000));
  }
}


void setup() {
  Serial.begin(115200);
  if (!oled.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    while (true);  // Halt
  }
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
  oled.setCursor(0, 0);
  oled.println("Booting...");
  oled.display();

  EEPROM.begin(EEPROM_SIZE);

  pinMode(IR_PIN, INPUT);
  pinMode(LIGHT_PIN, OUTPUT);
  digitalWrite(LIGHT_PIN, LOW);
  pinMode(C_IN, INPUT);
  pinMode(RESET_BTN, INPUT);
  analogReadResolution(12); // Default is 12 bits (0-4095)


  display.begin();
  display.setIntensity(8);
  display.displayClear();

  loadConfig();

  // Try to connect WiFi with stored credentials
  if (strlen(config.ssid) > 0) {
    Serial.printf("Connecting to WiFi SSID: %s\n", config.ssid);
    WiFi.begin(config.ssid, config.pass);
    unsigned long startAttemptTime = millis();

    // Wait 10 seconds for connection
    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
      oled.clearDisplay();
      oled.setTextSize(1);
      oled.setCursor(0, 0);
      oled.println("Connecting WiFi...");
      oled.println(config.ssid);
      oled.display();

      delay(500);
      Serial.print(".");
    }
  }

  if (WiFi.status() != WL_CONNECTED) {
    oled.clearDisplay();
    oled.setTextSize(1);
    oled.setCursor(0, 0);
    oled.println("AP Mode: CyroTime");
    oled.println("IP: 192.168.4.1");
    oled.display();

    Serial.println("\nFailed to connect WiFi, starting AP mode.");
    startAPMode();
  } else {
    Serial.println("\nWiFi connected!");
    timeClient.begin();
    timeClient.update();
    oled.clearDisplay();
    oled.setCursor(0, 0);
    oled.setTextSize(1);
    oled.println("WiFi Connected!");
    oled.println(WiFi.localIP());
    oled.display();


   

    // Start MQTT client
    mqttClient.setServer(config.mqtt, config.mqtt_port);
    mqttClient.setCallback(mqttCallback);

    // Start dual-core tasks
    xTaskCreatePinnedToCore(
      IR_MQTT_Task,
      "IR_MQTT_Task",
      4096,
      NULL,
      1,
      NULL,
      0);  // Core 0

    xTaskCreatePinnedToCore(
      Display_Task,
      "Display_Task",
      4096,
      NULL,
      1,
      NULL,
      1);  // Core 1
    // ✅ Finally: Start OLED status task
    xTaskCreatePinnedToCore(
      OLED_Status_Task,
     "OLED_Status",
     4096,
     NULL,
     1,
     NULL,
     1);  // Core 1
  }
}

void loop() {
  if (digitalRead(RESET_BTN) == HIGH){
    oled.clearDisplay();
    oled.setTextSize(1);
    oled.setCursor(0, 0);
    oled.println("Initing CyroTime Reset Protocal.");
    oled.display();
    delay(7000);
    ESP.restart();
  }
  if (WiFi.status() == WL_CONNECTED) {
    if (!mqttClient.connected()) {
      connectMQTT();
    }
  } else {
    server.handleClient();
  }
}

     
