// PawSaver: ESP32 Solar-Powered Ground Temperature Sensor
// Features: MLX90614 IR sensor, MQTT, HomeAssistant, WiFi setup via captive portal, deep sleep, battery management
// Author: (Your Name)

#include <WiFi.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <esp_task_wdt.h>
#include <Arduino.h>
#include <DNSServer.h>
#include <esp_sleep.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Adafruit_MLX90614.h>
#include <MQTT.h>

// --- Pin Definitions ---
#define BATTERY_PIN 34 // GPIO for battery voltage (adjust as needed)
#define WAKEUP_PIN 10  // GPIO for external wakeup (check board pinout)

// --- Power Management ---
#define MS_TO_S_FACTOR 1000ULL
#define US_TO_S_FACTOR 1000000ULL
#define TIME_TO_SLEEP_DEBUG  10
#define TIME_TO_SLEEP_SHORT  60
#define TIME_TO_SLEEP_LONG  300
#define TIME_TO_SLEEP_DEAD  3600
#define ON_INTERVAL         20000
#define WDT_TIMEOUT         250

// --- Battery Voltage Thresholds ---
const float BATTERY_MIN_VOLTAGE = 3.73;
const float BATTERY_LOW_VOLTAGE = 3.84;
const float BATTERY_FULL_VOLTAGE = 4.02;
const float PLUGGED_IN = 4.4;

// --- Modes ---
enum Mode { NORMAL, LOW_POWER, DEAD, DEBUG, AP_MODE };

// --- WiFi/MQTT Credentials (default, can be overwritten by captive portal) ---
struct Settings {
  char wifi_ssid[40] = "NatsNet";
  char wifi_password[40] = "curiosity";
  char mqtt_broker_address[40] = "homeassistant.local";
  int mqtt_port = 1883;
  char mqtt_client_id[40] = "arduino";
  char mqtt_username[40] = "mqtt_user";
  char mqtt_password[40] = "EWB";
  char publish_topic[40] = "arduino1";
  char subscribe_topic[40] = "arduino1";
};
Settings settings;

// --- Globals ---
DNSServer dnsServer;
AsyncWebServer server(80);
WiFiClient network;
MQTTClient mqtt(256);
Adafruit_MLX90614 mlx;

unsigned long lastPublishTime = 0;
unsigned long wakeTime = 0;
int on_interval = ON_INTERVAL;
Mode mode = NORMAL;
bool wifi_data_received = false;
bool name_received = false;
bool password_received = false;
String user_name;
String wifi_password_input;

// --- HTML for Captive Portal ---
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html><head>
  <title>PawSaver WiFi Setup</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  </head><body>
  <h3>PawSaver WiFi Setup</h3>
  <form action="/get">
    <br>
    WiFi SSID: <input type="text" name="ssid">
    <br>
    WiFi Password: <input type="password" name="password">
    <br>
    <input type="submit" value="Submit">
  </form>
</body></html>)rawliteral";

// --- Function Prototypes ---
void print_wakeup_reason();
void start_wifi();
void setup_mlx90614();
void connect_mqtt();
void sendToMQTT();
void messageHandler(String &topic, String &payload);
void enter_deep_sleep();
void run_normal_mode(float v_batt);
void run_conservation_mode(float v_batt);
void run_very_low_battery_mode(float v_batt);
void debug_mode(float v_batt);
void setupServer();
void AP_Mode();
float read_battery_voltage();
void save_settings();
void load_settings();

// --- Implementation ---
void print_wakeup_reason() {
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  switch(wakeup_reason) {
    case ESP_SLEEP_WAKEUP_EXT0 : Serial.println("Wakeup: external signal (RTC_IO)"); break;
    case ESP_SLEEP_WAKEUP_EXT1 : Serial.println("Wakeup: external signal (RTC_CNTL)"); break;
    case ESP_SLEEP_WAKEUP_TIMER : Serial.println("Wakeup: timer"); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD : Serial.println("Wakeup: touchpad"); break;
    case ESP_SLEEP_WAKEUP_ULP : Serial.println("Wakeup: ULP program"); break;
    default : Serial.printf("Wakeup not from deep sleep: %d\n",wakeup_reason); break;
  }
}

void setup() {
  Serial.begin(9600);
  delay(1000);
  pinMode(WAKEUP_PIN, INPUT);
  print_wakeup_reason();
  wakeTime = millis();
  load_settings();
  setup_mlx90614();
  float v_batt = read_battery_voltage();
  if (v_batt >= PLUGGED_IN) mode = DEBUG;
  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0) mode = AP_MODE;
  switch(mode) {
    case NORMAL: run_normal_mode(v_batt); break;
    case LOW_POWER: run_conservation_mode(v_batt); break;
    case DEAD: run_very_low_battery_mode(v_batt); break;
    case DEBUG: debug_mode(v_batt); break;
    case AP_MODE: AP_Mode(); break;
    default: run_normal_mode(v_batt); break;
  }
  esp_sleep_enable_ext0_wakeup((gpio_num_t)WAKEUP_PIN, LOW);
  start_wifi();
  connect_mqtt();
}

void loop() {
  mqtt.loop();
  if (millis() - lastPublishTime > 5000) {
    sendToMQTT();
    lastPublishTime = millis();
  }
  if (millis() - wakeTime > on_interval) {
    enter_deep_sleep();
  }
}

void start_wifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(settings.wifi_ssid, settings.wifi_password);
  Serial.println("Connecting to Wi-Fi...");
  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if (millis() - startAttempt > 15000) { // 15s timeout
      Serial.println("\nWiFi connect failed, entering AP mode.");
      AP_Mode();
      return;
    }
  }
  Serial.println("\nConnected to WiFi");
}

void setup_mlx90614() {
  if (!mlx.begin()) {
    Serial.println("Error connecting to MLX90614. Check wiring.");
    while (1);
  }
  Serial.print("Emissivity = "); Serial.println(mlx.readEmissivity());
}

void connect_mqtt() {
  mqtt.begin(settings.mqtt_broker_address, settings.mqtt_port, network);
  mqtt.onMessage(messageHandler);
  Serial.print("Connecting to MQTT broker");
  unsigned long startAttempt = millis();
  while (!mqtt.connect(settings.mqtt_client_id, settings.mqtt_username, settings.mqtt_password)) {
    Serial.print(".");
    delay(100);
    if (millis() - startAttempt > 10000) {
      Serial.println("\nMQTT connect failed.");
      return;
    }
  }
  Serial.println("\nMQTT Connected!");
  mqtt.subscribe(settings.subscribe_topic);
}

void sendToMQTT() {
  StaticJsonDocument<200> message;
  float t_mlx = mlx.readAmbientTempC();
  float object = mlx.readObjectTempC();
  float v_batt = read_battery_voltage();
  message["timestamp"] = millis();
  message["ambient"] = t_mlx;
  message["object"] = object;
  message["battery"] = v_batt;
  message["mode"] = mode;
  char messageBuffer[256];
  serializeJson(message, messageBuffer);
  mqtt.publish(settings.publish_topic, messageBuffer);
  Serial.print("Published to MQTT: ");
  Serial.println(messageBuffer);
}

void messageHandler(String &topic, String &payload) {
  Serial.println("Received from MQTT:");
  Serial.println("- topic: " + topic);
  Serial.println("- payload: " + payload);
}

void enter_deep_sleep() {
  Serial.println("Going to sleep now");
  Serial.flush();
  esp_deep_sleep_start();
}

void run_normal_mode(float v_batt) {
  Serial.println("Normal mode. Battery good.");
  mode = NORMAL;
  on_interval = ON_INTERVAL;
  int time_to_sleep = TIME_TO_SLEEP_SHORT;
  if (v_batt < BATTERY_MIN_VOLTAGE) { mode = DEAD; time_to_sleep = TIME_TO_SLEEP_DEAD; }
  else if (v_batt < BATTERY_LOW_VOLTAGE) { mode = LOW_POWER; time_to_sleep = TIME_TO_SLEEP_LONG; }
  else if (v_batt >= PLUGGED_IN) { mode = DEBUG; time_to_sleep = TIME_TO_SLEEP_SHORT; }
  esp_sleep_enable_timer_wakeup(time_to_sleep * US_TO_S_FACTOR);
}

void run_conservation_mode(float v_batt) {
  Serial.println("Battery conservation mode.");
  mode = LOW_POWER;
  on_interval = ON_INTERVAL;
  int time_to_sleep = TIME_TO_SLEEP_LONG;
  if (v_batt < BATTERY_MIN_VOLTAGE) { mode = DEAD; time_to_sleep = TIME_TO_SLEEP_DEAD; }
  else if (v_batt >= BATTERY_FULL_VOLTAGE) { mode = NORMAL; time_to_sleep = TIME_TO_SLEEP_SHORT; }
  else if (v_batt >= PLUGGED_IN) { mode = DEBUG; time_to_sleep = TIME_TO_SLEEP_SHORT; }
  esp_sleep_enable_timer_wakeup(time_to_sleep * US_TO_S_FACTOR);
}

void run_very_low_battery_mode(float v_batt) {
  Serial.println("Battery is dead! Waiting to charge.");
  mode = DEAD;
  on_interval = ON_INTERVAL;
  int time_to_sleep = TIME_TO_SLEEP_DEAD;
  if (v_batt >= PLUGGED_IN) { mode = DEBUG; time_to_sleep = TIME_TO_SLEEP_SHORT; }
  else if (v_batt >= BATTERY_FULL_VOLTAGE) { mode = NORMAL; time_to_sleep = TIME_TO_SLEEP_SHORT; }
  else if (v_batt <= BATTERY_LOW_VOLTAGE) { mode = LOW_POWER; time_to_sleep = TIME_TO_SLEEP_LONG; }
  esp_sleep_enable_timer_wakeup(time_to_sleep * US_TO_S_FACTOR);
}

void debug_mode(float v_batt) {
  Serial.println("Plugged in. Debug mode.");
  mode = DEBUG;
  on_interval = 60000;
  int time_to_sleep = TIME_TO_SLEEP_DEBUG;
  if (v_batt < PLUGGED_IN) { mode = NORMAL; time_to_sleep = TIME_TO_SLEEP_DEAD; }
  esp_sleep_enable_timer_wakeup(time_to_sleep * US_TO_S_FACTOR);
}

// --- Captive Portal ---
class CaptiveRequestHandler : public AsyncWebHandler {
public:
  CaptiveRequestHandler() {}
  bool canHandle(AsyncWebServerRequest *request) override { return true; }
  void handleRequest(AsyncWebServerRequest *request) override {
    request->send_P(200, "text/html", index_html);
  }
};

void setupServer() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html);
  });
  server.on("/get", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("ssid")) {
      String ssid = request->getParam("ssid")->value();
      strncpy(settings.wifi_ssid, ssid.c_str(), sizeof(settings.wifi_ssid));
      name_received = true;
    }
    if (request->hasParam("password")) {
      String pwd = request->getParam("password")->value();
      strncpy(settings.wifi_password, pwd.c_str(), sizeof(settings.wifi_password));
      password_received = true;
    }
    if (name_received && password_received) {
      save_settings();
      wifi_data_received = true;
    }
    request->send(200, "text/html", "Settings saved. <a href=\"/\">Return</a>");
  });
}

void AP_Mode() {
  Serial.println("Setting up AP Mode");
  WiFi.mode(WIFI_AP);
  WiFi.softAP("PawSaver-Setup");
  setupServer();
  dnsServer.start(53, "*", WiFi.softAPIP());
  server.addHandler(new CaptiveRequestHandler());
  server.begin();
  while (!wifi_data_received) {
    dnsServer.processNextRequest();
    delay(100);
  }
  server.end();
  dnsServer.stop();
  ESP.restart();
}

float read_battery_voltage() {
  int raw = analogRead(BATTERY_PIN);
  float v_in = raw * 3.3 / 4095.0;
  float v_batt = v_in * 1.487; // Adjust divider as needed
  return v_batt;
}

// --- Settings Storage (EEPROM/NVS) ---
void save_settings() {
  // TODO: Implement persistent storage (EEPROM, NVS, or SPIFFS)
}
void load_settings() {
  // TODO: Implement persistent storage (EEPROM, NVS, or SPIFFS)
}
