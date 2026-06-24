
// 1. Include the official MAVLink 2.0 library headers
#include "mavlink/common/mavlink.h"
#include <time.h>

// --- WiFi & API Configuration ---
const char* ssid = "2.4";
const char* password = "password";
String apiKey = "**************************";
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <time.h>


// --- Hardware Serial Setup (Pins 16 and 17) ---
HardwareSerial mavlinkSerial(2);

// --- State Tracking Variables ---
bool gotGpsFix = false;
double targetLat = 0.0;
double targetLon = 0.0;

// --- Timing Configuration (15 Minutes) ---
const unsigned long interval = 900000;
unsigned long previousMillis = 0;

// Onboard companion computer identifiers
const uint8_t system_id = 1;
const uint8_t component_id = 191;

// --- Helper Functions ---
void connectWiFi();
void fetchAndSendMavlinkWeather(double lat, double lon);
void sendMavlinkStatusText(const char* text);
void epochToTimeStr(long epoch, char* buffer, size_t size);

void setup() {
  Serial.begin(115200);
  mavlinkSerial.begin(57600, SERIAL_8N1, 16, 17);

  delay(1000);
  Serial.println("\n--- MAVLink 2.0 Advanced Weather Node Starting ---");
  connectWiFi();
}

void loop() {
  unsigned long currentMillis = millis();
  mavlink_message_t msg;
  mavlink_status_t status;

  // 1. Sniff Telemetry for 3D GPS Fix (Global Position Int - ID 33)
  while (mavlinkSerial.available() > 0) {
    uint8_t c = mavlinkSerial.read();
    if (mavlink_parse_char(MAVLINK_COMM_0, c, &msg, &status)) {
      if (msg.msgid == MAVLINK_MSG_ID_GLOBAL_POSITION_INT) {
        mavlink_global_position_int_t pos;
        mavlink_msg_global_position_int_decode(&msg, &pos);
        if (pos.lat != 0 && pos.lon != 0) {
          targetLat = pos.lat / 10000000.0;
          targetLon = pos.lon / 10000000.0;
          gotGpsFix = true;
        }
      }
    }
  }

  // 2. Fetch and Inject Weather Data every 15 minutes
  if (gotGpsFix) {
    if (currentMillis - previousMillis >= interval || previousMillis == 0) {
      if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n[Timer] Triggering 15-minute weather update...");
        fetchAndSendMavlinkWeather(targetLat, targetLon);
        previousMillis = currentMillis;
      } else {
        connectWiFi();
      }
    }
  }
}

void connectWiFi() {
  Serial.print("[WiFi] Connecting...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" Connected!");
}

void epochToTimeStr(long epoch, char* buffer, size_t size) {
  time_t rawtime = (time_t)epoch;
  struct tm* timeinfo = gmtime(&rawtime);
  strftime(buffer, size, "%H:%M", timeinfo);
}

void fetchAndSendMavlinkWeather(double lat, double lon) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;

  String locationStr = String(lat, 5) + "," + String(lon, 5);
  String serverPath = "https://weather.visualcrossing.com/VisualCrossingWebServices/rest/services/timeline/"
                      + locationStr + "?unitGroup=metric"
                      + "&elements=temp,feelslike,tempmax,tempmin,dew,precip,precipprob,preciptype,snow,snowdepth,windspeed,windgust,winddir,cloudcover,visibility,sunriseEpoch,sunsetEpoch,moonphase,uvindex,solarradiation"
                      + "&include=current,days&key=" + apiKey + "&contentType=json";

  http.begin(client, serverPath);
  int httpResponseCode = http.GET();

  if (httpResponseCode == 200) {
    String payload = http.getString();

    JsonDocument filter;
    filter["currentConditions"]["temp"] = true;
    filter["currentConditions"]["feelslike"] = true;
    filter["currentConditions"]["dew"] = true;
    filter["currentConditions"]["precip"] = true;
    filter["currentConditions"]["precipprob"] = true;
    filter["currentConditions"]["preciptype"][0] = true;
    filter["currentConditions"]["snow"] = true;
    filter["currentConditions"]["snowdepth"] = true;
    filter["currentConditions"]["windspeed"] = true;
    filter["currentConditions"]["windgust"] = true;
    filter["currentConditions"]["winddir"] = true;
    filter["currentConditions"]["cloudcover"] = true;
    filter["currentConditions"]["visibility"] = true;
    filter["currentConditions"]["uvindex"] = true;
    filter["currentConditions"]["solarradiation"] = true;
    filter["days"][0]["tempmax"] = true;
    filter["days"][0]["tempmin"] = true;
    filter["days"][0]["sunriseEpoch"] = true;
    filter["days"][0]["sunsetEpoch"] = true;
    filter["days"][0]["moonphase"] = true;

    JsonDocument doc;
    deserializeJson(doc, payload, DeserializationOption::Filter(filter));

    // Data Mapping
    float temp = doc["currentConditions"]["temp"] | 0.0;
    float feels = doc["currentConditions"]["feelslike"] | 0.0;
    float dew = doc["currentConditions"]["dew"] | 0.0;
    float tempmax = doc["days"][0]["tempmax"] | 0.0;
    float tempmin = doc["days"][0]["tempmin"] | 0.0;
    float precip = doc["currentConditions"]["precip"] | 0.0;
    float precipProb = doc["currentConditions"]["precipprob"] | 0.0;
    const char* pType = doc["currentConditions"]["preciptype"][0] | "none";
    float snow = doc["currentConditions"]["snow"] | 0.0;
    float snowDepth = doc["currentConditions"]["snowdepth"] | 0.0;
    float windSpeed = doc["currentConditions"]["windspeed"] | 0.0;
    float windGust = doc["currentConditions"]["windgust"] | 0.0;
    int windDir = doc["currentConditions"]["winddir"] | 0;
    float cloud = doc["currentConditions"]["cloudcover"] | 0.0;
    float vis = doc["currentConditions"]["visibility"] | 0.0;
    float uv = doc["currentConditions"]["uvindex"] | 0.0;
    float sol = doc["currentConditions"]["solarradiation"] | 0.0;
    long sunriseE = doc["days"][0]["sunriseEpoch"] | 0;
    long sunsetE = doc["days"][0]["sunsetEpoch"] | 0;
    float moon = doc["days"][0]["moonphase"] | 0.0;

    char sr[6], ss[6];
    epochToTimeStr(sunriseE, sr, sizeof(sr));
    epochToTimeStr(sunsetE, ss, sizeof(ss));
    // --- Aggressive Compression (Strict 50-char limit) ---
    // --- MAVLink Buffer Formatting (8 Messages) ---
    char buf[8][50];

    snprintf(buf[0], 50, "WX 1/8: T:%.1fC Feel:%.1fC Dew:%.1fC", temp, feels, dew);
    snprintf(buf[1], 50, "WX 2/8: Max:%.1fC Min:%.1fC", tempmax, tempmin);
    snprintf(buf[2], 50, "WX 3/8: Rain:%.1fmm(%.0f%%) Type:%s", precip, precipProb, pType);
    snprintf(buf[3], 50, "WX 4/8: Snow:%.1fmm Depth:%.1fmm", snow, snowDepth);
    snprintf(buf[4], 50, "WX 5/8: Wind:%.1f G:%.1fkm/h Dir:%d", windSpeed, windGust, windDir);
    snprintf(buf[5], 50, "WX 6/8: Cloud:%.0f%% Vis:%.1fkm", cloud, vis);
    snprintf(buf[6], 50, "WX 7/8: UV:%.1f Rad:%.0fW/m2", uv, sol);
    snprintf(buf[7], 50, "WX 8/8: SR:%s SS:%s MP:%.2f", sr, ss, moon);

    // --- Transmission with 1-Second Pacing ---
    Serial.println("\n[MAVLink TX] Injecting 8-Part Sequence (1s intervals):");
    for (int i = 0; i < 8; i++) {
      Serial.print("  -> Sending: ");
      Serial.println(buf[i]);
      sendMavlinkStatusText(buf[i]);
      delay(1000);  // 1-second delay between status text packets
    }
    Serial.println("[MAVLink TX] Transmission burst complete.");
  }
  http.end();
}

void sendMavlinkStatusText(const char* text) {
  mavlink_message_t msg;
  uint8_t buf[MAVLINK_MAX_PACKET_LEN];
  mavlink_msg_statustext_pack(system_id, component_id, &msg, MAV_SEVERITY_INFO, text, 0, 0);
  uint16_t len = mavlink_msg_to_send_buffer(buf, &msg);
  mavlinkSerial.write(buf, len);
}
