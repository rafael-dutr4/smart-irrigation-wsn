#include <esp_now.h>
#include <esp_wifi.h>
#include <WiFi.h>
#include <time.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager WiFi Configuration Magic

constexpr char DEFAULT_HOST[] = "";
constexpr int DEFAULT_CHANNEL = 9;

constexpr char NODE_NAME[] = "ESP32-NODE-GATEAWAY";
constexpr int BLUE_LED_PIN = 32;

String backend_host;
int wifi_channel;

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

int last_soil_moisture;
int last_water_level;
int last_solenoid_state;

bool send_soil_moisture;
bool send_water_level;
bool send_solenoid_state;

NodePayload payload;

WiFiManager wm;

WiFiManagerParameter backend_host_field;
WiFiManagerParameter wifi_channel_field;

String getParam(String name){
  String value;
  if(wm.server->hasArg(name)) {
    value = wm.server->arg(name);
  }
  return value;
}

void save_params() {
  backend_host = getParam("backend_host");
  wifi_channel = getParam("wifi_channel").toInt();
}

void setup_wifi() {
  Serial.println("Setting up WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);

  new (&backend_host_field) WiFiManagerParameter("backend_host", "Host para sincronizar os dados", DEFAULT_HOST, 140, "");
  new (&wifi_channel_field) WiFiManagerParameter("wifi_channel", "Canal WiFi (quando desconectado)", String(DEFAULT_CHANNEL).c_str(), 2, "type='number'");

  wm.addParameter(&backend_host_field);
  wm.addParameter(&wifi_channel_field);
  wm.setSaveParamsCallback(save_params);
  wm.setSaveConfigCallback(save_params);
  wm.setDarkMode(true); 
  wm.setConfigPortalTimeout(120);

  bool res;
  res = wm.autoConnect(NODE_NAME, "password"); 
  if(!res) {
    Serial.println("Failed to connect to the WiFi or hit timeout");
    esp_wifi_set_promiscuous(true);
    WiFi.setChannel(wifi_channel);
    esp_wifi_set_promiscuous(false);
  } 
  else {
    Serial.println("connected to the WiFi...yeey :)");
    WiFi.disconnect();
  }    

  Serial.println("\nWi-Fi setup complete.");
}

void on_receive_message(const esp_now_recv_info_t* node_info, const uint8_t* incoming_data, int len) {
    memcpy(&payload, incoming_data, sizeof(payload));
    Serial.println("Mensagem recebida.");

    switch (payload.type){
      case soilMoisture:
        last_soil_moisture = payload.value;
        send_soil_moisture = true;
        break;
      case waterLevel: 
        last_water_level = payload.value;
        send_water_level = true;
        break; 
      case solenoidState: 
        last_solenoid_state = payload.value;
        send_solenoid_state = true;
    }

    digitalWrite(BLUE_LED_PIN, HIGH);
    delay(500);
    digitalWrite(BLUE_LED_PIN, LOW);
}

void setup_esp_now() {
    Serial.println("Initializing ESP-NOW...");
    if (esp_now_init() != ESP_OK) {
        Serial.println("ESP-NOW initialization failed.");
        return;
    }
    esp_now_register_recv_cb(on_receive_message);
    Serial.println("ESP-NOW setup complete.");
}

void setup_sensor() {
  Serial.println("Configuring sensor...");
  pinMode(BLUE_LED_PIN, OUTPUT);
  digitalWrite(BLUE_LED_PIN, LOW);
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

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("Starting setup...");
  backend_host = DEFAULT_HOST;
  wifi_channel = DEFAULT_CHANNEL;
  last_soil_moisture = 0;
  last_water_level = 0;
  last_solenoid_state = 0;
  send_soil_moisture = false;
  send_water_level = false;
  send_solenoid_state = false;

  setup_wifi();
  setup_esp_now();
  setup_sensor();
  Serial.println("Setup complete.");
  print_node_info();
}

void send_to_backend(String payload, String url) {
  Serial.println("Sending data to the backend.");
  WiFi.reconnect();
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED){
    Serial.print(".");
    delay(1000);
  }

  HTTPClient http;
  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  int http_response_code = http.POST(payload);
  if (http_response_code != 201) {
    Serial.printf("\nHTTP request failed. Response (%d): %s\n", http_response_code, http.getString());
  }
  http.end();
  WiFi.disconnect();
  Serial.println("Data was sent to the backend.");
}

void send_variable_to_backend(String type){
  if (type == "soil_moisture"){
    send_soil_moisture = false;
  } else {
    send_water_level = false;
  }

  StaticJsonDocument<200> json_doc;
  json_doc["tipo_variavel"] = type;
  json_doc["valor"] = last_soil_moisture;

  String payload;
  serializeJson(json_doc, payload);

  String url = backend_host + "/variavel";
  send_to_backend(payload, url);
}

void send_solenoid_state_to_backend(){
  send_solenoid_state = false;

  StaticJsonDocument<200> json_doc;
  json_doc["status"] = last_solenoid_state ? "aberto" : "fechado";
  String payload;
  serializeJson(json_doc, payload);

  String url = backend_host + "/solenoide";
  send_to_backend(payload, url);
}

void loop() {
  if (send_soil_moisture) {
    send_variable_to_backend("soil_moisture");
  } 
  
  if (send_water_level) {
    send_variable_to_backend("water_level");
  } 
  
  if (send_solenoid_state) {
    send_solenoid_state_to_backend();
  } 

  delay(500);
}
