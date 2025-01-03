#include <esp_now.h>
#include <esp_wifi.h>
#include <WiFi.h>
#include <time.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager WiFi Configuration Magic

// Node Information
const char* NODE_NAME = "ESP32-NODE-RESERVOIR";
constexpr uint8_t CONTROL_NODE_MAC_ADDRESS[] = {0xC8, 0x2E, 0x18, 0x68, 0x8B, 0xB4};
constexpr uint8_t GATEWAY_NODE_MAC_ADDRESS[] = {0x8C, 0x4F, 0x00, 0x3D, 0x52, 0x84};

constexpr int BLUE_LED_PIN = 14; // Blink when transfer data with the control node
constexpr int RED_LED_PIN = 13; // Blink when tranfer data with the gateway node
constexpr int ECHO_PIN = 32; // Brown
constexpr int TRIGGER_PIN = 25; // Yellow
constexpr int DEFAULT_WIFI_CHANNEL = 9;
constexpr int DEFAULT_MAX_OF_TRIES = 20;
constexpr int RESERVOIR_HEIGHT_CM = 178;
constexpr int DEFAULT_INTERVAL_READ = 14400; // 4 hours

int interval_read;
int wifi_channel;
int no_max_of_tries;

esp_now_peer_info_t gateway_node_info;
esp_now_peer_info_t control_node_info;

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

int water_level;
bool send_data_to_control_node = false;
bool waiting_control_node_ack = false;
bool send_data_to_gateway = false;
bool waiting_gateway_ack = false;
int no_of_tries = 0;
time_t previous_read_time = time(NULL);


WiFiManager wm;
WiFiManagerParameter no_max_of_tries_field;
WiFiManagerParameter wifi_channel_field;
WiFiManagerParameter interval_read_field;

/** Setup Functions **/

String getParam(String name){
  //read parameter from server, for customhmtl input
  String value;
  if(wm.server->hasArg(name)) {
    value = wm.server->arg(name);
  }
  return value;
}

void save_params() {
  no_max_of_tries = getParam("no_max_of_tries").toInt();
  wifi_channel = getParam("wifi_channel").toInt();
  interval_read = getParam("interval_read").toInt();
}

// Configure Wi-Fi
void setup_wifi() {
  Serial.println("Setting up Wi-Fi...");
  WiFi.mode(WIFI_STA);    
  WiFi.setSleep(false);

  new (&no_max_of_tries_field) WiFiManagerParameter("no_max_of_tries", "Máximo de reenvios de dados", String(DEFAULT_MAX_OF_TRIES).c_str(), 3, "type='number'");
  new (&wifi_channel_field) WiFiManagerParameter("wifi_channel", "Canal WiFi (quando desconectado)", String(DEFAULT_WIFI_CHANNEL).c_str(), 2, "type='number'");
  new (&interval_read_field) WiFiManagerParameter("interval_read", "Intervalo de leitura do nível de reservatório de água", String(DEFAULT_INTERVAL_READ).c_str(), 10, "type='number'");

  wm.resetSettings();

  wm.addParameter(&no_max_of_tries_field);
  wm.addParameter(&wifi_channel_field);
  wm.addParameter(&interval_read_field);

  wm.setSaveParamsCallback(save_params);
  wm.setSaveConfigCallback(save_params);
  wm.setBreakAfterConfig(true);

  wm.setDarkMode(true);

  wm.setConfigPortalTimeout(120);

  bool res;
  wm.autoConnect(NODE_NAME, "password");
  esp_wifi_set_promiscuous(true);
  WiFi.setChannel(wifi_channel);
  esp_wifi_set_promiscuous(false);

  Serial.println("\nWi-Fi setup complete.");
}


void on_receive_message(const esp_now_recv_info_t* node_info, const uint8_t* incoming_data, int len) {
    memcpy(&payload, incoming_data, sizeof(payload));

    switch (payload.type) {
      case requestWaterLevel:
        if (!send_data_to_control_node) {
          send_data_to_control_node = true;
        }
    }

    digitalWrite(BLUE_LED_PIN, HIGH);
    delay(500);
    digitalWrite(BLUE_LED_PIN, LOW);
}

void on_message_sent(const uint8_t* mac_addr, esp_now_send_status_t status) {  
  Serial.println(no_of_tries);
  switch (status) {
    case ESP_NOW_SEND_SUCCESS:
      no_of_tries = 1;
      if (waiting_control_node_ack) {
        digitalWrite(BLUE_LED_PIN, HIGH);
        send_data_to_gateway = true;
      } else {
        digitalWrite(RED_LED_PIN, HIGH);
      }      
      break;
    case ESP_NOW_SEND_FAIL:
      if (no_of_tries >= no_max_of_tries) {
        no_of_tries = 1;
        break;
      }
      no_of_tries++;
      
      // Force a resend
      send_data_to_gateway = true;
      if (waiting_control_node_ack) {
        send_data_to_control_node = true;
      } else {
        send_data_to_gateway = true;
      }      
  }
  delay(1000);
  digitalWrite(RED_LED_PIN, LOW);
  digitalWrite(BLUE_LED_PIN, LOW);
  waiting_gateway_ack = false;
  waiting_control_node_ack = false;
}

// Configure ESP-NOW
void setup_esp_now() {
    Serial.println("Initializing ESP-NOW...");
    if (esp_now_init() != ESP_OK) {
        Serial.println("ESP-NOW initialization failed.");
        return;
    }
    esp_now_register_recv_cb(on_receive_message);
    esp_now_register_send_cb(on_message_sent);

    memcpy(gateway_node_info.peer_addr, GATEWAY_NODE_MAC_ADDRESS, 6);
    gateway_node_info.encrypt = false;

    if (esp_now_add_peer(&gateway_node_info) != ESP_OK) {
      Serial.println("Error adding ESP-NOW peer.");
      return;
    }
    
    memcpy(control_node_info.peer_addr, CONTROL_NODE_MAC_ADDRESS, 6);
    control_node_info.encrypt = false;

    if (esp_now_add_peer(&control_node_info) != ESP_OK) {
      Serial.println("Error adding ESP-NOW peer.");
      return;
    }

    Serial.println("ESP-NOW setup complete.");
}

// Configure GPIO Pins
void setup_pins() {
    Serial.println("Configuring pins...");
    pinMode(BLUE_LED_PIN, OUTPUT);
    pinMode(RED_LED_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);
    pinMode(TRIGGER_PIN, OUTPUT);
    digitalWrite(BLUE_LED_PIN, LOW);
    digitalWrite(RED_LED_PIN, LOW);
    Serial.println("Pin configuration complete.");
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

int read_sensor_data() {
  long duration;
  float distance;

  digitalWrite(TRIGGER_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIGGER_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIGGER_PIN, LOW);

  duration = pulseIn(ECHO_PIN, HIGH, 12000);

  distance = (duration * 0.0343) / 2;

  float water_level = RESERVOIR_HEIGHT_CM - distance;

  int percentual = map(water_level, 0, RESERVOIR_HEIGHT_CM, 0, 100);

  Serial.print("Distance to water: ");
  Serial.print(distance);
  Serial.println(" cm");
  
  Serial.print("Water Height: ");
  Serial.print(water_level);
  Serial.println(" cm");
  
  Serial.print("Water Level: ");
  Serial.print(percentual);
  Serial.println("%");

  previous_read_time = time(NULL);

  return percentual;
}

bool is_time_to_read() {
  return difftime(time(NULL), previous_read_time) >= interval_read;
}

void send_esp_now_data(const uint8_t* mac_address) {
  Serial.println("Sending data to another node.");

  payload.type = waterLevel;
  payload.value = read_sensor_data();

  esp_err_t send_result = esp_now_send(mac_address, (uint8_t*)&payload, sizeof(payload));
  if (send_result == ESP_OK) {
    Serial.println("The message was sent.");
  } else {
    Serial.println("The message was NOT sent.");
  }

  Serial.println("Data was sent to another node.");
}

// Setup Function
void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("Initializing system...");
    wifi_channel = DEFAULT_WIFI_CHANNEL;
    no_max_of_tries = DEFAULT_MAX_OF_TRIES;
    interval_read = DEFAULT_INTERVAL_READ;

    setup_wifi();
    setup_esp_now();
    setup_pins();

    previous_read_time -= interval_read;
    Serial.println("System initialization complete.");
    print_node_info();
}

// Loop Function
void loop() {
  if (send_data_to_control_node) {
    send_data_to_control_node = false;
    send_esp_now_data(CONTROL_NODE_MAC_ADDRESS);
    waiting_control_node_ack = true;
  }

  if (is_time_to_read() || send_data_to_gateway) {
    send_data_to_gateway = false;
    send_esp_now_data(GATEWAY_NODE_MAC_ADDRESS);
    waiting_gateway_ack = true;
  }    
}
