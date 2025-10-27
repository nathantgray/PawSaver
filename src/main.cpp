  #include <WiFi.h>
  #include <ArduinoJson.h>
  #include <Wire.h>
  //#include <Adafruit_Sensor.h>
  #include <esp_task_wdt.h>
  #include <Arduino.h>
  #include <DNSServer.h>
  #include <esp_sleep.h>

    #include <AsyncTCP.h>
    #include <ESPAsyncWebServer.h>
    #include <esp_sleep.h>
    #include <Adafruit_MLX90614.h>
    #include <MQTT.h>


  // Function declarations (prototypes)
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


  //#include "EEPROM.h"

  #define mS_TO_S_FACTOR 1000ULL  /* Conversion factor for milli seconds to seconds */
  #define uS_TO_S_FACTOR 1000000ULL  /* Conversion factor for micro seconds to seconds */
  #define TIME_TO_SLEEP_DEBUG  10        /* Time ESP32 will go to sleep (in seconds) */
  #define TIME_TO_SLEEP_SHORT  60        /* Time ESP32 will go to sleep (in seconds) */
  #define TIME_TO_SLEEP_LONG  300        /* Time ESP32 will go to sleep (in seconds) */
  #define TIME_TO_SLEEP_DEAD  3600        /* Time ESP32 will go to sleep (in seconds) */
  #define NORMAL 0
  #define LOW_POWER 1
  #define DEAD 2
  #define DEBUG 3
  #define AP_MODE 4
  #define WDT_TIMEOUT 250  // Watchdog timer in seconds.

  const int wakeupPin = 2;  // GPIO 7 for external wake-up


  const int PUBLISH_INTERVAL = 5000;  // 5 seconds
  const int ON_INTERVAL = 20000;  // milliseconds
  int on_interval = 20000;  // milliseconds
  //****************************************//
  // MQTT Configuration
  const char WIFI_SSID[] = "NatsNet";     // CHANGE TO YOUR WIFI SSID
  const char  WIFI_PASSWORD[] = "curiosity";  // CHANGE TO YOUR WIFI PASSWORD
  const char MQTT_BROKER_ADRRESS[] = "homeassistant.local";  // CHANGE TO MQTT BROKER'S ADDRESS
  const int MQTT_PORT = 1883;
  const char MQTT_CLIENT_ID[] = "arduino";  // CHANGE IT AS YOU DESIRE
  const char MQTT_USERNAME[] = "mqtt_user";                      // CHANGE IT IF REQUIRED, empty if not required
  const char MQTT_PASSWORD[] = "EWB";                      // CHANGE IT IF REQUIRED, empty if not required

  // The MQTT topics that ESP32 should publish/subscribe
  const char PUBLISH_TOPIC[] = "arduino1";    // CHANGE IT AS YOU DESIRE
  const char SUBSCRIBE_TOPIC[] = "arduino1";  // CHANGE IT AS YOU DESIRE

  //****************************************//
  // KOlby GIU setup
  DNSServer dnsServer;
  AsyncWebServer server(80);

  bool name_received = false;
  bool proficiency_received = false;
  bool WIFI_DATA_RECEIVED = false;

  String user_name;
  String proficiency;
  const char index_html[] PROGMEM = R"rawliteral(
  <!DOCTYPE HTML><html><head>
    <title>Captive Portal Demo</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    </head><body>
    <h3>Captive Portal Demo</h3>
    <br><br>
    <form action="/get">
      <br>
      Name: <input type="text" name="name">
      <br>
      Wifi Password: 
      <select name = "proficiency">
        <option value=curiosity>curiosity</option>
        <option value=Advanced>Advanced</option>
        <option value=Pro>Pro</option>
      </select>
      <input type="submit" value="Submit">
    </form>
  </body></html>)rawliteral";



  WiFiClient network;
  MQTTClient mqtt = MQTTClient(256);
  //****************************************//


  int batteryPin = A3;
  //int BUTTON_PIN  = A0; // The GPIO pin connected to the button
  Adafruit_MLX90614 mlx = Adafruit_MLX90614();


  const float BATTERY_MIN_VOLTAGE = 3.73;
  const float BATTERY_LOW_VOLTAGE = 3.84;
  const float BATTERY_FULL_VOLTAGE = 4.02;
  const float PLUGGED_IN = 4.4;
  unsigned long lastPublishTime = 0;
  unsigned long wakeTime = 0;


  int mode = NORMAL; 

  struct Settings {
    char wifi_ssid[40];
    char wifi_password[40];
    char mqtt_broker_address[40];
    int mqtt_port;
    char mqtt_client_id[40];
    char mqtt_username[40];
    char mqtt_password[40];
    char publish_topic[40];
    char subscibe_topic[40];
  } settings;



  void print_wakeup_reason(){
    esp_sleep_wakeup_cause_t wakeup_reason;

    wakeup_reason = esp_sleep_get_wakeup_cause();

    switch(wakeup_reason)
    {
      case ESP_SLEEP_WAKEUP_EXT0 : Serial.println("Wakeup caused by external signal using RTC_IO"); break;
      case ESP_SLEEP_WAKEUP_EXT1 : Serial.println("Wakeup caused by external signal using RTC_CNTL"); break;
      case ESP_SLEEP_WAKEUP_TIMER : Serial.println("Wakeup caused by timer"); break;
      case ESP_SLEEP_WAKEUP_TOUCHPAD : Serial.println("Wakeup caused by touchpad"); break;
      case ESP_SLEEP_WAKEUP_ULP : Serial.println("Wakeup caused by ULP program"); break;
      default : Serial.printf("Wakeup was not caused by deep sleep: %d\n",wakeup_reason); break;
    }
  }

  void setup() {
    Serial.begin(9600);
    delay(5000);
    pinMode(wakeupPin, INPUT);
    //esp_task_wdt_init(WDT_TIMEOUT, true);
    //esp_task_wdt_add(NULL);
    //esp_task_wdt_reset();
    Serial.println("Start setup");
    print_wakeup_reason();
    wakeTime = millis();
    //setup_mlx90614();

    float t_mlx = 0;
    float object = 0;
    float raw_analog_read = 0;
    float v_in = 0;
    float v_batt = 0;
    //t_mlx = mlx.readAmbientTempC();
    //object = mlx.readObjectTempC();
  // raw_analog_read = analogRead(batteryPin);
    v_in = raw_analog_read*3.3/4095;
    v_batt = v_in*1.487;
    
    if (v_batt >= PLUGGED_IN) {
      Serial.println("Plugged in.");
      int time_to_sleep = TIME_TO_SLEEP_SHORT;
      mode = DEBUG;
    }

    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_GPIO){
      mode = AP_MODE;
    }

    switch(mode)
    {
      case NORMAL : run_normal_mode(v_batt); break; 
      case LOW_POWER : run_conservation_mode(v_batt); break;
      case DEAD : run_very_low_battery_mode(v_batt); break;
      case DEBUG : debug_mode(v_batt); break;
      case AP_MODE : AP_Mode(); break;
      
      default : run_normal_mode(v_batt); break;
    }
    gpio_deep_sleep_wakeup_enable(gpio_num_t(wakeupPin), GPIO_INTR_LOW_LEVEL);


    Serial.println("mode: " + String(mode));
    // AP_Mode(); add mode and access it by button inttrupet need to check how to set up pins for inttrupet
    start_wifi();
    //connect_mqtt();
    Serial.println("Connected to broker( mqtt look is done)");

  }

  void loop() {
    //Serial.println("Entering Loop");
    //esp_task_wdt_reset();
    //mqtt.loop();
    //Serial.println("Entering after mqtt loop");
    if (millis() - lastPublishTime > PUBLISH_INTERVAL) {
      //sendToMQTT();
      lastPublishTime = millis();
      Serial.println("Pubished to mqtt");
    }
    if (millis() - wakeTime > on_interval) {
      enter_deep_sleep();
    } 

  }

  void start_wifi(){
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.println("Arduino Nano ESP32 - Connecting to Wi-Fi");
    int i = 0;
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
      // if(i>=60){
      //   // esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP_SHORT * uS_TO_S_FACTOR);
      //   // Serial.println("Setup ESP32 to sleep for " + String(TIME_TO_SLEEP_SHORT) +
      //   // " Seconds");
      //   // enter_deep_sleep(); // Failed to connect. Go back to sleep.
      //   esp_restart();
      // }
      // i++;
    }
    Serial.println("Connected to wifi");

  }

  void setup_mlx90614() {
    if (!mlx.begin()) {
      Serial.println("Error connecting to MLX sensor. Check wiring.");
      while (1);
    };
    Serial.print("Emissivity = "); Serial.println(mlx.readEmissivity());
    Serial.println("================================================");
  }

  void connect_mqtt() {
    // Connect to the MQTT broker
    mqtt.begin(MQTT_BROKER_ADRRESS, MQTT_PORT, network);

    // Create a handler for incoming messages
    mqtt.onMessage(messageHandler);

    Serial.print("Arduino Nano ESP32 - Connecting to MQTT broker");

    while (!mqtt.connect(MQTT_CLIENT_ID, MQTT_USERNAME, MQTT_PASSWORD)) {
      Serial.print(".");
      delay(100);
    }
    Serial.println();

    if (!mqtt.connected()) {
      Serial.println("Arduino Nano ESP32 - MQTT broker Timeout!");
      return;
    }

    // Subscribe to a topic, the incoming messages are processed by messageHandler() function
    if (mqtt.subscribe(SUBSCRIBE_TOPIC))
      Serial.print("Arduino Nano ESP32 - Subscribed to the topic: ");
    else
      Serial.print("Arduino Nano ESP32 - Failed to subscribe to the topic: ");

    Serial.println(SUBSCRIBE_TOPIC);
    Serial.println("Arduino Nano ESP32 - MQTT broker Connected!");
  }

  void sendToMQTT() {
    StaticJsonDocument<200> message;

    float t_mlx = 0;
    float object = 0;
    float raw_analog_read = 0;
    float v_in = 0;
    float v_batt = 0;
    t_mlx = mlx.readAmbientTempC();
    object = mlx.readObjectTempC();
    raw_analog_read = analogRead(batteryPin);
    v_in = raw_analog_read*3.3/4095;
    v_batt = v_in*1.487;
    message["timestamp"] = millis();
    message["ambient"] = t_mlx;
    message["object"] = object;
    message["battery"] = v_batt;
    message["mode"] = mode;
    //https://docs.espressif.com/projects/esp-idf/en/v4.4/esp32s3/api-reference/peripherals/adc.html
    char messageBuffer[512];
    serializeJson(message, messageBuffer);

    mqtt.publish(PUBLISH_TOPIC, messageBuffer);

    Serial.println("Arduino Nano ESP32 - sent to MQTT:");
    Serial.print("- topic: ");
    Serial.println(PUBLISH_TOPIC);
    Serial.print("- payload:");
    Serial.println(messageBuffer);
  }

  void messageHandler(String &topic, String &payload) {
    Serial.println("Arduino Nano ESP32 - received from MQTT:");
    Serial.println("- topic: " + topic);
    Serial.println("- payload:");
    Serial.println(payload);
  }

  void enter_deep_sleep(){
      Serial.println("Going to sleep now");
      Serial.flush(); 
      esp_deep_sleep_start();
  }

  void run_normal_mode(float v_batt){
    Serial.println("Running in normal mode. Battery charge is good.");
    int time_to_sleep = TIME_TO_SLEEP_SHORT;
    mode = NORMAL;
    on_interval = ON_INTERVAL;
    if (v_batt < BATTERY_MIN_VOLTAGE) {
      int time_to_sleep = TIME_TO_SLEEP_DEAD;
      mode = DEAD;
    }
    if (v_batt < BATTERY_LOW_VOLTAGE) {
      int time_to_sleep = TIME_TO_SLEEP_DEAD;
      Serial.println("Change to lowe power mode.");
      mode = LOW_POWER;
    }
    if (v_batt >= PLUGGED_IN) {
      Serial.println("Plugged in.");
      int time_to_sleep = TIME_TO_SLEEP_SHORT;
      mode = DEBUG;
    }
    esp_sleep_enable_timer_wakeup(time_to_sleep * uS_TO_S_FACTOR);
    Serial.println("Setup ESP32 to sleep for " + String(time_to_sleep) +
    " Seconds in mode" + String(mode));
  }

  void run_conservation_mode(float v_batt){
    Serial.println("Battery conservation mode.");
    int time_to_sleep = TIME_TO_SLEEP_LONG;
    mode = LOW_POWER;
    on_interval = ON_INTERVAL;
    if (v_batt < BATTERY_MIN_VOLTAGE) {
      int time_to_sleep = TIME_TO_SLEEP_DEAD;
      mode = DEAD;
    }
    if (v_batt >= BATTERY_FULL_VOLTAGE) {
      int time_to_sleep = TIME_TO_SLEEP_SHORT;
      mode = NORMAL;
    }
    if (v_batt >= PLUGGED_IN) {
      Serial.println("Plugged in.");
      int time_to_sleep = TIME_TO_SLEEP_SHORT;
      mode = DEBUG;
    }
    esp_sleep_enable_timer_wakeup(time_to_sleep * uS_TO_S_FACTOR);
    Serial.println("Setup ESP32 to sleep for " + String(time_to_sleep) +
    " Seconds");

  }

  void run_very_low_battery_mode(float v_batt){

    Serial.println("Battery is dead! Waiting to charge.");
    int time_to_sleep = TIME_TO_SLEEP_DEAD;
    mode = DEAD;
    on_interval = ON_INTERVAL;
    if (v_batt >= PLUGGED_IN) {
      Serial.println("Plugged in.");
      int time_to_sleep = TIME_TO_SLEEP_SHORT;
      mode = DEBUG;
    }
    if (v_batt >= BATTERY_FULL_VOLTAGE) {
      int time_to_sleep = TIME_TO_SLEEP_SHORT;
      mode = NORMAL;
    }
    if (v_batt <= BATTERY_LOW_VOLTAGE){
      int time_to_sleep = TIME_TO_SLEEP_LONG;
      mode = LOW_POWER;
    }
    esp_sleep_enable_timer_wakeup(time_to_sleep * uS_TO_S_FACTOR);
    Serial.println("Setup ESP32 to sleep for " + String(time_to_sleep) +
    " Seconds");
    
  }

  void debug_mode(float v_batt){

    Serial.println("Plugged in. Running in debug mode.");
    int time_to_sleep = TIME_TO_SLEEP_DEBUG;
    mode = DEBUG;
    on_interval = 60000;  // stay on for 1 minutes
    if (v_batt < PLUGGED_IN) {
      int time_to_sleep = TIME_TO_SLEEP_DEAD;
      Serial.println("Unplugged.");
      mode = NORMAL;
    }
    esp_sleep_enable_timer_wakeup(time_to_sleep * uS_TO_S_FACTOR);
    Serial.println("Setup ESP32 to sleep for " + String(time_to_sleep) +
    " Seconds");
  }
  // KOLby GUI
  class CaptiveRequestHandler : public AsyncWebHandler {
  public:
    CaptiveRequestHandler() {}
    virtual ~CaptiveRequestHandler() {}

    bool canHandle(AsyncWebServerRequest *request){
      //request->addInterestingHeader("ANY");
      return true;
    }

    void handleRequest(AsyncWebServerRequest *request) {
      request->send_P(200, "text/html", index_html); 
    }
  };

  void setupServer(){
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send_P(200, "text/html", index_html); 
        Serial.println("Client Connected");
    });
      
    server.on("/get", HTTP_GET, [] (AsyncWebServerRequest *request) {
        String inputMessage;
        String inputParam;
    
        if (request->hasParam("name")) {
          inputMessage = request->getParam("name")->value();
          inputParam = "name";
          //WIFI_SSID = inputMessage;
          Serial.println(inputMessage);
          name_received = true;
        }

        if (request->hasParam("proficiency")) {
          inputMessage = request->getParam("proficiency")->value();
          inputParam = "proficiency";
          //WIFI_PASSWORD = inputMessage;
          Serial.println(inputMessage);
          proficiency_received = true;
        }
        request->send(200, "text/html", "The values entered by you have been successfully sent to the device <br><a href=\"/\">Return to Home Page</a>");
    });
  }

  void AP_Mode() // should set up and the captival port for arudion and the when reviec data kick out and shut down DNS and Async webserver
  {
    Serial.println("Setting up AP Mode"); // Gerneral setup for captival port 
    WiFi.mode(WIFI_AP); 
    WiFi.softAP("esp-captive");
    Serial.print("AP IP address: ");Serial.println(WiFi.softAPIP());
    Serial.println("Setting up Async WebServer");
    setupServer();
    Serial.println("Starting DNS Server");
    dnsServer.start(53, "*", WiFi.softAPIP());
    server.addHandler(new CaptiveRequestHandler()).setFilter(ON_AP_FILTER);//only when requested from AP
    //more handlers...
    server.begin();
    Serial.println("All Done!");
    while (WIFI_DATA_RECEIVED == false) // Beginning of connection loop 
    {
      dnsServer.processNextRequest();
      if(name_received && proficiency_received)
      {
        Serial.print("Hello ");Serial.println(user_name);
        Serial.print("You have stated your proficiency to be ");Serial.println(proficiency);

        name_received = false;
        proficiency_received = false;
        WIFI_DATA_RECEIVED = true; // reset values and kick out of while
        Serial.println("We'll wait for the next client now");
        server.end();  // shuts down DNS and Async webserver to allow easy connection to WIFI
        dnsServer.stop();
      }
    }
    
  }