#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <UniversalTelegramBot.h>

// ====== CONFIGURATION ======
const char* ssid = "Subzero";
const char* password = "Subzero123";

const char* mqtt_server = "192.168.0.110";  // Home Assistant IP
const int mqtt_port = 1883;
const char* mqtt_user = "admin";
const char* mqtt_pass = "admin";

#define BOT_TOKEN "7836044607:AAFhsiVAEXySLIROhY9PhezeX7gVXbajKf4"
#define CHAT_ID "6348432516"  // Your Telegram User ID

#define LED_PIN 2 // GPIO4

// MQTT Topics
const char* mqtt_cmd_topic = "home/led/control";
const char* mqtt_state_topic = "home/led/state";

// ====== OBJECTS ======
WiFiClient espClient;
PubSubClient client(espClient);
WiFiClientSecure secureClient;
UniversalTelegramBot bot(BOT_TOKEN, secureClient);

bool ledState = true;
unsigned long lastTelegramCheck = 0;

// ====== FUNCTIONS ======
void publishDiscoveryConfig() {
  const char* discoveryTopic = "homeassistant/light/esp_led/config";
  String payload = "{";
  payload += "\"name\": \"ESP LED\",";
  payload += "\"state_topic\": \"home/led/state\",";
  payload += "\"command_topic\": \"home/led/control\",";
  payload += "\"payload_on\": \"ON\",";
  payload += "\"payload_off\": \"OFF\",";
  payload += "\"retain\": true";
  payload += "}";

  client.publish(discoveryTopic, payload.c_str(), true);  // retained
}
void connectWiFi() {
  Serial.print("Connecting to WiFi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" connected!");
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (int i = 0; i < length; i++) message += (char)payload[i];

  Serial.printf("MQTT [%s]: %s\n", topic, message.c_str());

  if (String(topic) == mqtt_cmd_topic) {
    if (message == "ON") {
      digitalWrite(LED_PIN, HIGH);
      ledState = true;
      client.publish(mqtt_state_topic, "ON", true);
      bot.sendMessage(CHAT_ID, "LED turned ON via Home Assistant ✅", "");
    } else if (message == "OFF") {
      digitalWrite(LED_PIN, LOW);
      ledState = false;
      client.publish(mqtt_state_topic, "OFF", true);
      bot.sendMessage(CHAT_ID, "LED turned OFF via Home Assistant ❌", "");
    }
  }
}

void reconnectMQTT() {
  while (!client.connected()) {
    Serial.print("Connecting to MQTT...");
    if (client.connect("LEDClient", mqtt_user, mqtt_pass)) {
      Serial.println(" connected");

      // Subscribe to command topic
      client.subscribe(mqtt_cmd_topic);

      // Publish current LED state
      client.publish(mqtt_state_topic, ledState ? "ON" : "OFF", true);

      // Publish MQTT Discovery config
      publishDiscoveryConfig();
    } else {
      Serial.print(" failed, rc=");
      Serial.print(client.state());
      Serial.println(" retrying in 2 seconds...");
      delay(2000);
    }
  }
}


void handleTelegramMessages() {
  int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
  while (numNewMessages) {
    for (int i = 0; i < numNewMessages; i++) {
      String text = bot.messages[i].text;
      String chat_id = bot.messages[i].chat_id;

      if (chat_id != CHAT_ID) continue;

      if (text == "/on") {
        digitalWrite(LED_PIN, HIGH);
        ledState = true;
        bot.sendMessage(CHAT_ID, "LED is ON ✅", "");
        client.publish(mqtt_state_topic, "ON", true);
      } else if (text == "/off") {
        digitalWrite(LED_PIN, LOW);
        ledState = false;
        bot.sendMessage(CHAT_ID, "LED is OFF ❌", "");
        client.publish(mqtt_state_topic, "OFF", true);
      } else if (text == "/status") {
        bot.sendMessage(CHAT_ID, ledState ? "LED is ON 🔆" : "LED is OFF 🌑", "");
      }
    }
    numNewMessages = bot.getUpdates(bot.last_message_received + 1);
  }
}

// ====== SETUP ======
void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  connectWiFi();
  secureClient.setInsecure();  // Skip certificate validation

  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(mqttCallback);
}

// ====== LOOP ======
void loop() {
  if (!client.connected()) reconnectMQTT();
  client.loop();

  if (millis() - lastTelegramCheck > 3000) {
    handleTelegramMessages();
    lastTelegramCheck = millis();
  }
}