#include <esp_now.h>
#include <esp_wifi.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager WiFi Configuration Magic

// Node Information
const char* NODE_NAME = "ESP32-NODE-CONTROL";
constexpr uint8_t GATEWAY_NODE_MAC_ADDRESS[] = {0x8C, 0x4F, 0x00, 0x3D, 0x52, 0x84};
constexpr uint8_t RESERVOIR_NODE_MAC_ADDRESS[] = {0x8C, 0x4F, 0x00, 0x3D, 0x0E, 0x70};

constexpr int BLUE_LED_PIN = 32;
constexpr int GREEN_LED_PIN = 13;
constexpr int RELAY_PIN = 25;
constexpr int DEFAULT_MIN_SOIL_MOISTURE = 60;
constexpr int DEFAULT_MAX_SOIL_MOISTURE = 90;
constexpr int DEFAULT_WIFI_CHANNEL = 9;
constexpr int DEFAULT_MAX_OF_TRIES = 20;
constexpr int DEFAULT_MIN_RESERVOIR = 30;

int min_soil_moisture;
int max_soil_moisture;
int wifi_channel;
int no_max_of_tries;
int min_reservoir;

esp_now_peer_info_t gateway_node_info;
esp_now_peer_info_t reservoir_node_info;

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

int soil_moisture;
bool solenoid_open = false;
bool send_data_to_gateway = false;
bool waiting_gateway_ack = false;
bool request_reservoir_state = false;
bool waiting_reservoir_ack = false;
int no_of_tries = 0;

WiFiManager wm;
WiFiManagerParameter min_soil_moisture_field;
WiFiManagerParameter max_soil_moisture_field;
WiFiManagerParameter no_max_of_tries_field;
WiFiManagerParameter wifi_channel_field;
WiFiManagerParameter min_reservoir_field;

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
  min_soil_moisture = getParam("min_soil_moisture").toInt();
  max_soil_moisture = getParam("max_soil_moisture").toInt();
  no_max_of_tries = getParam("no_max_of_tries").toInt();
  wifi_channel = getParam("wifi_channel").toInt();
  min_reservoir = getParam("min_reservoir").toInt();
}

// Configure Wi-Fi
void setup_wifi() {
  Serial.println("Setting up Wi-Fi...");
  WiFi.mode(WIFI_STA);    
  WiFi.setSleep(false);

  new (&min_soil_moisture_field) WiFiManagerParameter("min_soil_moisture", "Nível mínimo de umidade do solo (%)", String(DEFAULT_MIN_SOIL_MOISTURE).c_str(), 3, "type='number'");
  new (&max_soil_moisture_field) WiFiManagerParameter("max_soil_moisture", "Nível máximo de umidade do solo (%)", String(DEFAULT_MAX_SOIL_MOISTURE).c_str(), 3, "type='number'");
  new (&min_reservoir_field) WiFiManagerParameter("min_reservoir", "Nível mínimo do reservatório de água (%)", String(DEFAULT_MIN_RESERVOIR).c_str(), 3, "type='number'");
  new (&no_max_of_tries_field) WiFiManagerParameter("no_max_of_tries", "Máximo de reenvios de dados", String(DEFAULT_MAX_OF_TRIES).c_str(), 3, "type='number'");
  new (&wifi_channel_field) WiFiManagerParameter("wifi_channel", "Canal WiFi (quando desconectado)", String(DEFAULT_WIFI_CHANNEL).c_str(), 2, "type='number'");

  wm.resetSettings();

  wm.addParameter(&min_soil_moisture_field);
  wm.addParameter(&max_soil_moisture_field);
  wm.addParameter(&no_max_of_tries_field);
  wm.addParameter(&wifi_channel_field);
  wm.addParameter(&min_reservoir_field);

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
      case waterLevel:
        if (payload.value < min_reservoir) {
          solenoid_open = false;
          break;
        }  
        
        if (!solenoid_open) {
          solenoid_open = true;
          send_data_to_gateway = true;
        }
      case soilMoisture:
        if (payload.value < min_soil_moisture && !solenoid_open) {
          request_reservoir_state = true;
        } else if (payload.value > max_soil_moisture && solenoid_open) {
          solenoid_open = false;
          send_data_to_gateway = true;
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
      digitalWrite(GREEN_LED_PIN, HIGH);
      break;
    case ESP_NOW_SEND_FAIL:
      if (no_of_tries >= no_max_of_tries) {
        // If the communication with the reservoir node break, the system will
        // continue the irrigation process without the water level information.
        if (waiting_reservoir_ack && !solenoid_open) {
          solenoid_open = true;
          send_data_to_gateway;
        }
        no_of_tries = 1;
        break;
      }
      no_of_tries++;
      
      // Force a resend
      if (waiting_gateway_ack) {
        send_data_to_gateway = true;
      } else {
        request_reservoir_state = true;
      }
  }
  delay(1000);
  digitalWrite(GREEN_LED_PIN, LOW);
  waiting_gateway_ack = false;
  waiting_reservoir_ack = false;
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
    
    memcpy(reservoir_node_info.peer_addr, RESERVOIR_NODE_MAC_ADDRESS, 6);
    reservoir_node_info.encrypt = false;

    if (esp_now_add_peer(&reservoir_node_info) != ESP_OK) {
      Serial.println("Error adding ESP-NOW peer.");
      return;
    }

    Serial.println("ESP-NOW setup complete.");
}

// Configure GPIO Pins
void setup_pins() {
    Serial.println("Configuring pins...");
    pinMode(GREEN_LED_PIN, OUTPUT);
    pinMode(BLUE_LED_PIN, OUTPUT);
    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, HIGH);  // Relay off
    digitalWrite(BLUE_LED_PIN, LOW);
    digitalWrite(GREEN_LED_PIN, LOW);
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

/** Main Logic **/

void activate_solenoid() {
  if (digitalRead(RELAY_PIN) == HIGH) {
    digitalWrite(RELAY_PIN, LOW); 
  }  
}
void deactivate_solenoid() {
  if (digitalRead(RELAY_PIN) == LOW) {
    digitalWrite(RELAY_PIN, HIGH);
  }  
}

// Setup Function
void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("Initializing system...");
    min_soil_moisture = DEFAULT_MIN_SOIL_MOISTURE;
    max_soil_moisture = DEFAULT_MAX_SOIL_MOISTURE;
    wifi_channel = DEFAULT_WIFI_CHANNEL;
    no_max_of_tries = DEFAULT_MAX_OF_TRIES;
    min_reservoir = DEFAULT_MIN_RESERVOIR;

    setup_wifi();
    setup_esp_now();
    setup_pins();
    Serial.println("System initialization complete.");
    print_node_info();
}

void send_esp_now_message(const uint8_t* mac) {
  esp_err_t send_result = esp_now_send(mac, (uint8_t*)&payload, sizeof(payload));
  if (send_result == ESP_OK) {
    Serial.println("The message was sent.");
  } else {
    Serial.println("The message was NOT sent.");
  }
}


// Loop Function
void loop() {
    if (solenoid_open) {
      activate_solenoid();
    } else {
      deactivate_solenoid();
    }

    if (request_reservoir_state) {
      request_reservoir_state = false;
      payload.type = requestWaterLevel;

      send_esp_now_message(RESERVOIR_NODE_MAC_ADDRESS);
    }

    if (send_data_to_gateway) {
      send_data_to_gateway = false;
      payload.type = solenoidState;
      payload.value = solenoid_open ? 1 : 0;

      send_esp_now_message(GATEWAY_NODE_MAC_ADDRESS);
    }
}
