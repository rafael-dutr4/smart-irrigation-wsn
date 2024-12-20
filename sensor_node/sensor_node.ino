#include <esp_now.h>
#include <esp_wifi.h>
#include <WiFi.h>
#include <time.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager WiFi Configuration Magic

constexpr char NODE_NAME[] = "ESP32-NODE-SOIL-MOISTURE";
constexpr uint8_t CONTROL_NODE_MAC_ADDRESS[] = {0xC8, 0x2E, 0x18, 0x68, 0x8B, 0xB4};
constexpr uint8_t GATEWAY_NODE_MAC_ADDRESS[] = {0x88, 0x13, 0xBF, 0x00, 0xE5, 0xD0};

constexpr int SENSOR_MAX_VALUE = 4095;
constexpr int SENSOR_MIN_VALUE = 0;

constexpr int SENSOR_PIN = 34;
constexpr int HIGH_SENSOR_PIN = 32;
constexpr int BLUE_LED_PIN = 27;
constexpr int GREEN_LED_PIN = 13;
constexpr int RESET_CONFIGURATION_PIN = 14;

int READ_INTERVAL_SECONDS;
int NO_OF_SAMPLES;
int NO_MAX_OF_RETRIES;
int WIFI_CHANNEL;

bool waiting_gateway_ack = false;
bool waiting_control_node_ack = false;

bool send_to_gateway = false;
bool send_to_control_node = false;

time_t previous_read_time = time(NULL);

esp_now_peer_info_t control_node_info;
esp_now_peer_info_t gateway_node_info;

int no_of_retries = 0;

enum payload_type {
  soilMoisture,
  waterLevel, 
  solenoidState, 
  requestWaterLevel
};

struct NodePayload {
  payload_type type;
  int value;
};

NodePayload payload;

WiFiManager wm;

WiFiManagerParameter read_interval_seconds_field;
WiFiManagerParameter no_of_samples_field;
WiFiManagerParameter no_max_of_entries_field;
WiFiManagerParameter wifi_channel;

String getParam(String name){
  //read parameter from server, for customhmtl input
  String value;
  if(wm.server->hasArg(name)) {
    value = wm.server->arg(name);
  }
  return value;
}

void save_params() {
  Serial.println("Saving parameters");
  READ_INTERVAL_SECONDS = getParam("read_interval_seconds").toInt();
  NO_OF_SAMPLES = getParam("no_of_samples").toInt();
  NO_MAX_OF_RETRIES = getParam("no_max_of_entries").toInt();
  WIFI_CHANNEL = getParam("wifi_channel").toInt();
}

void setup_wifi() {
  Serial.println("Setting up WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);

  new (&read_interval_seconds_field) WiFiManagerParameter("read_interval_seconds", "Intervalo entre leituras (segundos)", "300", 4, "type='number'");
  new (&no_of_samples_field) WiFiManagerParameter("no_of_samples", "Número de amostras por leitura", "1", 3, "type='number'");
  new (&no_max_of_entries_field) WiFiManagerParameter("no_max_of_entries", "Máximo de reenvios de dados", "10", 3, "type='number'");
  new (&wifi_channel) WiFiManagerParameter("wifi_channel", "Canal WiFi (quando desconectado)", "11", 2, "type='number'");

  wm.addParameter(&read_interval_seconds_field);
  wm.addParameter(&no_of_samples_field);
  wm.addParameter(&no_max_of_entries_field);
  wm.addParameter(&wifi_channel);

  wm.setSaveParamsCallback(save_params);
  wm.setSaveConfigCallback(save_params);

  wm.setDarkMode(true);

  wm.setConfigPortalTimeout(120);
  wm.setBreakAfterConfig(true);

  bool res;
  // Open configuration portal.
  res = wm.autoConnect(NODE_NAME, "password"); 
  esp_wifi_set_promiscuous(true);
  WiFi.setChannel(WIFI_CHANNEL);
  esp_wifi_set_promiscuous(false);

  Serial.println("\nWi-Fi setup complete.");
}

void on_message_sent(const uint8_t* mac_addr, esp_now_send_status_t status) {  
  switch (status) {
    case ESP_NOW_SEND_SUCCESS:
      no_of_retries = 1;
      if (waiting_control_node_ack) {
        digitalWrite(GREEN_LED_PIN, HIGH);
        send_to_gateway = true;
      } else {
        digitalWrite(BLUE_LED_PIN, HIGH);
      }    
      break;
    case ESP_NOW_SEND_FAIL:
      if (no_of_retries == NO_MAX_OF_RETRIES) {
        no_of_retries = 1;
        break;
      }
      no_of_retries++;
      
      // Force a resend
      if (waiting_control_node_ack) {
        send_to_control_node = true;
      } else {
        send_to_gateway = true;
      }
  }
  delay(500);
  digitalWrite(GREEN_LED_PIN, LOW);
  digitalWrite(BLUE_LED_PIN, LOW);
  waiting_control_node_ack = false;
  waiting_gateway_ack = false;
}

void setup_esp_now() {
  Serial.println("Initializing ESP-NOW...");
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW.");
    return;
  }

  esp_now_register_send_cb(on_message_sent);
  memcpy(control_node_info.peer_addr, CONTROL_NODE_MAC_ADDRESS, 6);
  control_node_info.encrypt = false;

  if (esp_now_add_peer(&control_node_info) != ESP_OK) {
    Serial.println("Error adding ESP-NOW peer.");
    return;
  }

  memcpy(gateway_node_info.peer_addr, GATEWAY_NODE_MAC_ADDRESS, 6);
  gateway_node_info.encrypt = false;

  if (esp_now_add_peer(&gateway_node_info) != ESP_OK) {
    Serial.println("Error adding ESP-NOW peer.");
    return;
  }

  Serial.println("ESP-NOW setup complete.");
}

void setup_sensor() {
  Serial.println("Configuring sensor...");
  pinMode(SENSOR_PIN, INPUT);
  pinMode(HIGH_SENSOR_PIN, OUTPUT);
  pinMode(BLUE_LED_PIN, OUTPUT);
  pinMode(GREEN_LED_PIN, OUTPUT);
  pinMode(RESET_CONFIGURATION_PIN, INPUT);

  digitalWrite(HIGH_SENSOR_PIN, LOW);
  digitalWrite(BLUE_LED_PIN, LOW);
  digitalWrite(GREEN_LED_PIN, LOW);

  Serial.println("Sensor setup complete.");
}

void print_node_info() {
  Serial.println(NODE_NAME);
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  Serial.print("WiFi Channel: ");
  Serial.println(WiFi.channel());
  Serial.print("MAC Address: ");
  Serial.println(WiFi.macAddress());
}

long read_sensor_data() {
  digitalWrite(HIGH_SENSOR_PIN, HIGH);
  delay(5000);

  long total = 0;
  for (int i = 0; i < NO_OF_SAMPLES; i++) {
    total += analogRead(SENSOR_PIN);
    delay(500);
  }

  digitalWrite(HIGH_SENSOR_PIN, LOW);
  previous_read_time = time(NULL);
  long average = total / NO_OF_SAMPLES;
  Serial.printf("Sensor Resistance: %ld\n", average);
  return map(average, SENSOR_MIN_VALUE, SENSOR_MAX_VALUE, 100, 0);
}

bool is_time_to_read() {
  return difftime(time(NULL), previous_read_time) >= READ_INTERVAL_SECONDS;
}

void send_esp_now_data(const uint8_t* mac_address) {
  Serial.println("Sending data to another node.");

  payload.type = soilMoisture;
  payload.value = read_sensor_data();

  esp_err_t send_result = esp_now_send(mac_address, (uint8_t*)&payload, sizeof(payload));
  if (send_result == ESP_OK) {
    Serial.println("The message was sent.");
  } else {
    Serial.println("The message was NOT sent.");
  }

  Serial.println("Data was sent to another node.");
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("Starting setup...");
  // BACKEND_ENDPOINT = "";
  // READ_INTERVAL_SECONDS = 300; // 5 minutes
  // NO_OF_SAMPLES = 1;
  // NO_MAX_OF_RETRIES = 10; // 5 minutes
  previous_read_time -= READ_INTERVAL_SECONDS;

  setup_wifi();
  setup_esp_now();
  setup_sensor();
  Serial.println("Setup complete.");
  print_node_info();
}

void check_reset_button(){
  if ( digitalRead(RESET_CONFIGURATION_PIN) == HIGH ) {
    delay(50);
    if( digitalRead(RESET_CONFIGURATION_PIN) == HIGH ){
      Serial.println("Button Pressed");
      delay(10000);
      if( digitalRead(RESET_CONFIGURATION_PIN) == HIGH ){
        Serial.println("Button Held");
        Serial.println("Erasing Config, restarting");
        wm.resetSettings();
        ESP.restart();
      }
      
      Serial.println("Starting config portal");
      wm.setConfigPortalTimeout(120);

      char read_interval_seconds_buf[5];
      itoa(READ_INTERVAL_SECONDS, read_interval_seconds_buf, 10);
      read_interval_seconds_field.setValue(read_interval_seconds_buf, 4);
      char no_of_samples_buf[5];
      itoa(NO_OF_SAMPLES, no_of_samples_buf, 4);
      no_of_samples_field.setValue(no_of_samples_buf, 3);
      char no_max_of_retries_buf[5];
      itoa(NO_MAX_OF_RETRIES, no_max_of_retries_buf, 10);
      no_max_of_entries_field.setValue(no_max_of_retries_buf, 3);
      char wifi_channel_buf[3];
      itoa(WIFI_CHANNEL, wifi_channel_buf, 10);
      wifi_channel.setValue(wifi_channel_buf, 2);

      wm.startConfigPortal(NODE_NAME, "password");
    }
  }
}

void loop() {
  if (!waiting_control_node_ack && !waiting_gateway_ack) {
    if (is_time_to_read() || send_to_control_node) {
      send_to_control_node = false;
      send_esp_now_data(CONTROL_NODE_MAC_ADDRESS);
      waiting_control_node_ack = true;
    }

    if (send_to_gateway) {
      send_to_gateway = false;
      send_esp_now_data(GATEWAY_NODE_MAC_ADDRESS);
      waiting_gateway_ack = true;
    }
  }

  check_reset_button();
  delay(500);
}