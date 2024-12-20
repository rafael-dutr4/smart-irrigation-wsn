#include <esp_now.h>
#include <esp_wifi.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager WiFi Configuration Magic

// Node Information
const char* NODE_NAME = "ESP32-NODE-CONTROL";
constexpr uint8_t GATEWAY_NODE_MAC_ADDRESS[] = {0x88, 0x13, 0xBF, 0x00, 0xE5, 0xD0};
constexpr int BLUE_LED_PIN = 32;
constexpr int GREEN_LED_PIN = 13;
constexpr int RELAY_PIN = 25;
constexpr int RESET_CONFIGURATION_PIN = 14;

String BACKEND_ENDPOINT;
int MIN_SOIL_MOISTURE;
int MAX_SOIL_MOISTURE;
int WIFI_CHANNEL;

esp_now_peer_info_t gateway_node_info;

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
bool send_to_gateway = false;
bool waiting_gateway_ack = false;

WiFiManager wm;
WiFiManagerParameter min_soil_moisture_field;
WiFiManagerParameter max_soil_moisture_field;
WiFiManagerParameter no_max_of_entries_field;
WiFiManagerParameter wifi_channel;

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
  MIN_SOIL_MOISTURE = getParam("min_soil_moisture").toInt();
  MAX_SOIL_MOISTURE = getParam("max_soil_moisture").toInt();
  NO_MAX_OF_RETRIES = getParam("no_max_of_entries").toInt();
  WIFI_CHANNEL = getParam("wifi_channel").toInt();
}

// Configure Wi-Fi
void setup_wifi() {
  Serial.println("Setting up Wi-Fi...");
  WiFi.mode(WIFI_STA);    
  WiFi.setSleep(false);

  new (&min_soil_moisture_field) WiFiManagerParameter("min_soil_moisture", "Nível mínimo de umidade do solo (%)", "40", 3, "type='number'");
  new (&max_soil_moisture_field) WiFiManagerParameter("max_soil_moisture", "Nível máximo de umidade do solo (%)", "80", 3, "type='number'");
  new (&no_max_of_entries_field) WiFiManagerParameter("no_max_of_entries", "Máximo de reenvios de dados", "10", 3, "type='number'");
  new (&wifi_channel) WiFiManagerParameter("wifi_channel", "Canal WiFi (quando desconectado)", "11", 2, "type='number'");

  wm.addParameter(&min_soil_moisture_field);
  wm.addParameter(&max_soil_moisture_field);
  wm.addParameter(&no_max_of_entries_field);
  wm.addParameter(&wifi_channel);

  wm.setSaveParamsCallback(save_params);
  wm.setSaveConfigCallback(save_params);
  wm.setBreakAfterConfig(true);

  wm.setDarkMode(true);

  wm.setConfigPortalTimeout(120);

  bool res;
  wm.autoConnect(NODE_NAME, "password");
  esp_wifi_set_promiscuous(true);
  WiFi.setChannel(WIFI_CHANNEL);
  esp_wifi_set_promiscuous(false);

  Serial.println("\nWi-Fi setup complete.");
}


void on_receive_message(const esp_now_recv_info_t* node_info, const uint8_t* incoming_data, int len) {
    memcpy(&payload, incoming_data, sizeof(payload));

    switch (payload.type) {
      case soilMoisture:
        if (payload.soil_moisture < MIN_SOIL_MOISTURE) {
          if (!solenoid_open) {
            solenoid_open = true;
            send_to_gateway = true;
          }
        } else if (payload.soil_moisture > MAX_SOIL_MOISTURE) {
          if (solenoid_open) {
            solenoid_open = false;
            send_to_gateway = true;
          }
        }
    }

    digitalWrite(BLUE_LED_PIN, HIGH);
    delay(500);
    digitalWrite(BLUE_LED_PIN, LOW);
}

void on_message_sent(const uint8_t* mac_addr, esp_now_send_status_t status) {  
  switch (status) {
    case ESP_NOW_SEND_SUCCESS:
      no_of_retries = 1;
      digitalWrite(GREEN_LED_PIN, HIGH);
      break;
    case ESP_NOW_SEND_FAIL:
      if (no_of_retries == NO_MAX_OF_RETRIES) {
        no_of_retries = 1;
        break;
      }
      no_of_retries++;

      send_to_gateway
      
      // Force a resend
      if (waiting_control_node_ack) {
        send_to_control_node = true;
      } else {
        send_to_gateway = true;
      }
  }
  delay(500);
  digitalWrite(GREEN_LED_PIN, LOW);
  waiting_gateway_ack = false;
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

    Serial.println("ESP-NOW setup complete.");
}

// Configure GPIO Pins
void setup_pins() {
    Serial.println("Configuring pins...");
    pinMode(BLUE_LED_PIN, OUTPUT);
    pinMode(RELAY_PIN, OUTPUT);
    pinMode(RESET_CONFIGURATION_PIN, INPUT);
    digitalWrite(RELAY_PIN, HIGH);  // Relay off
    digitalWrite(BLUE_LED_PIN, LOW);
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

// Sync Data with Backend
void sync_with_backend() {
    WiFi.reconnect();
    Serial.print("Connecting to Wi-Fi for backend sync...");
    while (WiFi.status() != WL_CONNECTED) {
        Serial.print(".");
        delay(1000);
    }

    HTTPClient http;
    http.begin(BACKEND_ENDPOINT);
    http.addHeader("Content-Type", "application/json");

    StaticJsonDocument<200> json_doc;
    json_doc["status"] = solenoid_open ? "aberto" : "fechado";

    String payload;
    serializeJson(json_doc, payload);

    int http_response_code = http.POST(payload);
    if (http_response_code != 201) {
        Serial.printf("HTTP Request failed. Response: %s\n", http.getString().c_str());
    }
    http.end();
    WiFi.disconnect();
}


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
    BACKEND_ENDPOINT = "";
    MIN_SOIL_MOISTURE = 40;
    MAX_SOIL_MOISTURE = 80;
    WIFI_CHANNEL = 11;
    wifi_available = false;
    setup_wifi();
    setup_esp_now();
    setup_pins();
    Serial.println("System initialization complete.");
    print_node_info();
}

void check_reset_button(){
  // check for button press
  if ( digitalRead(RESET_CONFIGURATION_PIN) == HIGH ) {
    delay(50);
    if( digitalRead(RESET_CONFIGURATION_PIN) == HIGH ){
      Serial.println("Button Pressed");
      // still holding button for 3000 ms, reset settings
      delay(10000); // reset delay hold
      if( digitalRead(RESET_CONFIGURATION_PIN) == HIGH ){
        Serial.println("Button Held");
        Serial.println("Erasing Config, restarting");
        wm.resetSettings();
        ESP.restart();
      }
      
      // start portal w delay
      Serial.println("Starting config portal");
      wm.setConfigPortalTimeout(120);

      // Configurations
      char min_soil_moisture_buf[5];
      itoa(MIN_SOIL_MOISTURE, min_soil_moisture_buf, 10);
      min_soil_moisture_field.setValue(min_soil_moisture_buf, 4);
      char max_soil_moisture_buf[5];
      itoa(MAX_SOIL_MOISTURE, max_soil_moisture_buf, 10);
      max_soil_moisture_field.setValue(max_soil_moisture_buf, 4);
      char no_max_of_retries_buf[5];
      itoa(NO_MAX_OF_RETRIES, no_max_of_retries_buf, 10);
      no_max_of_entries_field.setValue(no_max_of_retries_buf, 3);
      char wifi_channel_buf[3];
      itoa(WIFI_CHANNEL, wifi_channel_buf, 10);
      wifi_channel.setValue(wifi_channel_buf, 2);
      
      if (!wm.startConfigPortal(NODE_NAME, "password")) {
        Serial.println("failed to connect or hit timeout");
        delay(3000);
        wifi_available = false;
        // ESP.restart();
      } else {
        //if you get here you have connected to the WiFi
        Serial.println("connected...yeey :)");
        wifi_available = true;
        WiFi.disconnect();
      }
    }
  }
}

// Loop Function
void loop() {
    check_reset_button();

    if (send_data_to_backend && wifi_available) {
      send_data_to_backend = false;
      sync_with_backend();
    }

    if (solenoid_open) {
      activate_solenoid();
    } else {
      deactivate_solenoid();
    }
}
