#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <base64.h>

#define DISABLE 0
#define ENABLE  1

#define SNIFFER_CHANNEL  1
#define BUFFER_SIZE  50

#define WIFI_SSID "PuppyApp"
#define WIFI_PASS "teddy04683"

#define SERVER_URL "http://192.168.1.8:3000/"

#define DATA_LENGTH           112

#define TYPE_MANAGEMENT       0x00
#define TYPE_CONTROL          0x01
#define TYPE_DATA             0x02
#define SUBTYPE_PROBE_REQUEST 0x04

struct RxControl {
 signed rssi:8;
 unsigned rate:4;
 unsigned is_group:1;
 unsigned:1;
 unsigned sig_mode:2;
 unsigned legacy_length:12;
 unsigned damatch0:1;
 unsigned damatch1:1;
 unsigned bssidmatch0:1;
 unsigned bssidmatch1:1;
 unsigned MCS:7; 
 unsigned CWB:1; 
 unsigned HT_length:16;
 unsigned Smoothing:1;
 unsigned Not_Sounding:1;
 unsigned:1;
 unsigned Aggregation:1;
 unsigned STBC:2;
 unsigned FEC_CODING:1; 
 unsigned SGI:1;
 unsigned rxend_state:8;
 unsigned ampdu_cnt:8;
 unsigned channel:4; 
 unsigned:12;
};

struct SnifferPacket{
    struct RxControl rx_ctrl;
    uint8_t data[DATA_LENGTH];
    uint16_t cnt;
    uint16_t len;
};

int data_l = 0;
boolean data_full = false;

struct Data {
  String mac;
  String time;
  int signal;
};

Data data[BUFFER_SIZE+50];

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);

  Serial.begin(115200);

  //Serial.setDebugOutput(true);

  Serial.print("\n");
  Serial.println("****************************");
  Serial.print("Chip id -> ");
  Serial.print(system_get_chip_id());
  Serial.print("\n");
  Serial.println("****************************");

  connect_to_wifi();
  make_request("register", String(micros()));

  delay(1000);

  set_sniffer_channel(SNIFFER_CHANNEL);
  delay(500);
  
}

void loop() {
  enable_sniffer();

  delay(100000);
  work();
  if(micros() > 2000000000) restart();
}

void work() {
  disable_sniffer();
  delay(50);
  connect_to_wifi();
  delay(50);
  send_req();
  delay(50);
  enable_sniffer();
}

static void getMAC(char *addr, uint8_t* data, uint16_t offset) {
  sprintf(addr, "%02x:%02x:%02x:%02x:%02x:%02x", data[offset+0], data[offset+1], data[offset+2], data[offset+3], data[offset+4], data[offset+5]);
}

void send_req(){
  for (int i = 0; i < data_l; i++) {
    make_request("data", String(data[i].mac + ";" + data[i].time + ";" + data[i].signal ));
  }
  data_l = 0;
  data_full = false;
}

boolean add_req(String mac, int signal){
  if(data_l >= BUFFER_SIZE){
    Serial.println("Buffer full");
    return false;
  } 
  data[data_l] = (Data) {mac, String(micros()), signal};
  data_l++;

  if(data_l >= BUFFER_SIZE) return false;
  return true;
}

void led_on(){
  digitalWrite(LED_BUILTIN, HIGH);
}

void led_off(){
  digitalWrite(LED_BUILTIN, LOW);
}

String make_request(String req, String payload) {
  
  String response;

  WiFiClient client;
  HTTPClient http;

  Serial.print("[HTTP] begin...\n");

  //payload = base64::encode(payload);
  String url = String(SERVER_URL + req + "?data=" + payload + "&id=" + system_get_chip_id());
  if (http.begin(client, url)) {
    Serial.print("[HTTP] GET...\n");
    Serial.println(url);
    int httpCode = http.GET();
    if (httpCode == 200) {
      Serial.printf("[HTTP] GET... code: %d\n", httpCode);
      response = http.getString();
      Serial.println(response);
    } else {
      Serial.printf("[HTTP] GET... failed, code: %d error: %s\n", httpCode, http.errorToString(httpCode).c_str());
      restart();
    }
    http.end();
  }

  return response;
}

void restart(){
  Serial.println("****************************");
  Serial.println("*     Restart in 2 sec     *");
  Serial.println("****************************");
  delay(2000);
  ESP.restart();
}

void connect_to_wifi(){
    
  led_on();

  WiFi.begin(WIFI_SSID, WIFI_PASS);  

  Serial.print("Connecting");
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(200);
    Serial.print(".");
  }
  Serial.println();

  Serial.print("Connected, IP address: ");
  Serial.println(WiFi.localIP());

  led_off();  
}

static void showMetadata(SnifferPacket *snifferPacket) {

  unsigned int frameControl = ((unsigned int)snifferPacket->data[1] << 8) + snifferPacket->data[0];

  uint8_t version      = (frameControl & 0b0000000000000011) >> 0;
  uint8_t frameType    = (frameControl & 0b0000000000001100) >> 2;
  uint8_t frameSubType = (frameControl & 0b0000000011110000) >> 4;
  uint8_t toDS         = (frameControl & 0b0000000100000000) >> 8;
  uint8_t fromDS       = (frameControl & 0b0000001000000000) >> 9;

  // Only look for probe request packets
  if (frameType != TYPE_MANAGEMENT ||
      frameSubType != SUBTYPE_PROBE_REQUEST)
        return;

  Serial.print("RSSI: ");
  Serial.print(snifferPacket->rx_ctrl.rssi, DEC);

  Serial.print(" Ch: ");
  Serial.print(wifi_get_channel());

  char addr[] = "00:00:00:00:00:00";
  getMAC(addr, snifferPacket->data, 10);
  Serial.print(" Peer MAC: ");
  Serial.print(addr);

  if(!add_req(String(addr), snifferPacket->rx_ctrl.rssi)) work();

  Serial.println();
}

static void ICACHE_FLASH_ATTR sniffer_callback(uint8_t *buffer, uint16_t length) {
  struct SnifferPacket *snifferPacket = (struct SnifferPacket*) buffer;
  showMetadata(snifferPacket);
}

void enable_sniffer() {
  Serial.println("enable_sniffer");
  wifi_station_clear_username();
  wifi_set_promiscuous_rx_cb(sniffer_callback);
  wifi_promiscuous_enable(ENABLE);
}

void disable_sniffer() {
  wifi_promiscuous_enable(DISABLE);
}

void set_sniffer_channel(uint8 channel) {
  wifi_set_channel(channel);
}