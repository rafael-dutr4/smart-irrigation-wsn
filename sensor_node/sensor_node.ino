#include <esp_now.h>
#include <esp_wifi.h>
#include <WiFi.h>
#include <time.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager WiFi Configuration Magic

constexpr char NODE_NAME[] = "ESP32-NODE-SOIL-MOISTURE";
constexpr uint8_t CONTROL_NODE_MAC_ADDRESS[] = {0xC8, 0x2E, 0x18, 0x68, 0x8B, 0xB4};
constexpr uint8_t GATEWAY_NODE_MAC_ADDRESS[] = {0x8C, 0x4F, 0x00, 0x3D, 0x52, 0x84};

constexpr int SENSOR_MAX_VALUE = 4095;
constexpr int SENSOR_MIN_VALUE = 0;
constexpr int DEFAULT_INTERVAL_READ = 300;
constexpr int DEFAULT_NO_MAX_OF_TRIES = 20;
constexpr int DEFAULT_WIFI_CHANNEL = 9;

constexpr int SENSOR_PIN = 34;
constexpr int HIGH_SENSOR_PIN = 32;
constexpr int BLUE_LED_PIN = 27;
constexpr int GREEN_LED_PIN = 13;

int read_interval_seconds;
int no_max_of_retries;
int wifi_channel;

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
WiFiManagerParameter no_max_of_entries_field;
WiFiManagerParameter wifi_channel_field;

String getParam(String name){
  String value;
  if(wm.server->hasArg(name)) {
    value = wm.server->arg(name);
  }
  Serial.print(name);
  Serial.print(":");
  Serial.println(value);
  return value;
}

void save_params() {
  Serial.println("Saving parameters");
  read_interval_seconds = getParam("read_interval_seconds").toInt();
  no_max_of_retries = getParam("no_max_of_entries").toInt();
  wifi_channel = getParam("wifi_channel").toInt();
}

void setup_wifi() {
  Serial.println("Setting up WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);

  new (&read_interval_seconds_field) WiFiManagerParameter("read_interval_seconds", "Intervalo entre leituras (segundos)", String(DEFAULT_INTERVAL_READ).c_str(), 4, "type='number'");
  new (&no_max_of_entries_field) WiFiManagerParameter("no_max_of_entries", "Máximo de reenvios de dados", String(DEFAULT_NO_MAX_OF_TRIES).c_str(), 3, "type='number'");
  new (&wifi_channel_field) WiFiManagerParameter("wifi_channel", "Canal WiFi (quando desconectado)", String(DEFAULT_WIFI_CHANNEL).c_str(), 2, "type='number'");

  wm.resetSettings();

  wm.addParameter(&read_interval_seconds_field);
  wm.addParameter(&no_max_of_entries_field);
  wm.addParameter(&wifi_channel_field);

  wm.setSaveParamsCallback(save_params);
  wm.setSaveConfigCallback(save_params);

  wm.setDarkMode(true);

  wm.setConfigPortalTimeout(120);
  wm.setBreakAfterConfig(true);

  bool res;
  // Open configuration portal.
  res = wm.autoConnect(NODE_NAME, "password"); 
  esp_wifi_set_promiscuous(true);
  WiFi.setChannel(wifi_channel);
  esp_wifi_set_promiscuous(false);

  Serial.println("\nWi-Fi setup complete.");
}

void on_message_sent(const uint8_t* mac_addr, esp_now_send_status_t status) { 
  Serial.print("Try:");
  Serial.println(no_of_retries); 
  switch (status) {
    case ESP_NOW_SEND_SUCCESS:
      Serial.println("Successfully sent");
      no_of_retries = 1;
      if (waiting_control_node_ack) {
        digitalWrite(GREEN_LED_PIN, HIGH);
        send_to_gateway = true;
      } else {
        digitalWrite(BLUE_LED_PIN, HIGH);
      }    
      break;
    case ESP_NOW_SEND_FAIL:
      Serial.println("Error on sent");
      Serial.println(no_max_of_retries);
      if (no_of_retries >= no_max_of_retries) {
        Serial.println("Stopping");
        no_of_retries = 1;
        break;
      }
      no_of_retries++;
      
      // Force a resend
      Serial.println("Trying again");
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
  long sensor_read = analogRead(SENSOR_PIN);
  delay(500);
  digitalWrite(HIGH_SENSOR_PIN, LOW);
  
  previous_read_time = time(NULL);
  Serial.printf("Sensor Resistance: %ld\n", sensor_read);
  return map(sensor_read, SENSOR_MIN_VALUE, SENSOR_MAX_VALUE, 100, 0);
}

bool is_time_to_read() {
  return difftime(time(NULL), previous_read_time) >= read_interval_seconds;
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

  read_interval_seconds = DEFAULT_INTERVAL_READ;
  no_max_of_retries = DEFAULT_NO_MAX_OF_TRIES;
  wifi_channel = DEFAULT_WIFI_CHANNEL;

  setup_wifi();
  setup_esp_now();
  setup_sensor();

  previous_read_time -= read_interval_seconds;
  Serial.println("Setup complete.");
  print_node_info();
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

  delay(500);
}