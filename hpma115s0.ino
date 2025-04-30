#include <SoftwareSerial.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>

#define DEBUG false
#define pin_rx D7
#define pin_tx D8

// WiFi and MQTT config
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

const char* mqtt_server = "YOUR_MQTT_BROKER_IP_OR_HOST";
const int mqtt_port = 1883;
const char* mqtt_user = "YOUR_MQTT_USERNAME";
const char* mqtt_pass = "YOUR_MQTT_PASSWORD";
const char* mqtt_topic = "Sensors/HPMA115S0/pm";

WiFiClient espClient;
PubSubClient client(espClient);

SoftwareSerial Device(pin_rx, pin_tx);

const int AutoSendOn[4] = {0x68, 0x01, 0x40, 0x57};
const int AutoSendOff[4] = {0x68, 0x01, 0x20, 0x77};
const int StartPmMeasure[4] = {0x68, 0x01, 0x01, 0x96};
const int StopPmMeasure[4] = {0x68, 0x01, 0x02, 0x95};
const int ReadPm[4] = {0x68, 0x01, 0x04, 0x93};

int isAutoSend = true;
int useReading = true;
int pm25 = 0;
int pm10 = 0;

unsigned long lastReading = 0;
unsigned long lastMqttPublish = 0;

void sendCommand(const int *cmd) {
  for(int i=0;i<4; i++) {
    Device.write(cmd[i]);
  }
  delay(10);
}

int readResponse(int l = 32) {
  int i = 0;
  int buf[l];
  unsigned long start = millis();

  while(Device.available() > 0 && i < l) {
    buf[i] = Device.read();
    if(DEBUG) {
      Serial.print("i: "); Serial.print(i);
      Serial.print(" buf[i]: "); Serial.println(buf[i], HEX);
    }
    if(i == 0 && !(buf[0] == 0x40 || buf[0] == 0x42 || buf[0] == 0xA5 || buf[0] == 0x96)) {
      if(DEBUG) Serial.println("Skipping Byte");
      continue;
    } else {
      i++;
    }

    if(buf[0] == 0x42 && buf[1] == 0x4d) l=32; // AutoSend
    if(buf[0] == 0x40 && buf[2] == 0x04) l=8;  // Manual read
    if(buf[0] == 0xA5 && buf[1] == 0xA5) return true; // ACK
    if(buf[0] == 0x96 && buf[1] == 0x96) return false; // NACK

    if (millis() - start > 1000) {
      Serial.println("Timeout");
      return false;
    }
  }

  if(buf[2] == 0x04) {
    int cs = buf[0] + buf[1] + buf[2];
    int c;
    for(c = 3; c < (2 + buf[1]); c++) cs += buf[c];
    cs = (65536 - cs) % 256;
    if(cs == buf[c]) {
      pm25 = buf[3] * 256 + buf[4];
      pm10 = buf[5] * 256 + buf[6];
      return true;
    } else {
      Serial.println("Checksum mismatch");
    }
  } else if(buf[3] == 0x1c) {
    int cs = 0;
    for(int c = 0; c <= buf[3]; c++) cs += buf[c];
    int checksum = buf[30] * 256 + buf[31];
    if(DEBUG) {
      Serial.print("Checksum: "); Serial.print(checksum, HEX);
      Serial.print(" CS: "); Serial.println(cs, HEX);
    }
    if(cs == checksum) {
      pm25 = buf[6] * 256 + buf[7];
      pm10 = buf[8] * 256 + buf[9];
      return true;
    } else {
      Serial.println("Checksum mismatch");
    }
  }

  return false;
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (client.connect("ESP8266Client", mqtt_user, mqtt_pass)) {
      Serial.println("connected");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  Device.begin(9600);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("WiFi connected");

  client.setServer(mqtt_server, mqtt_port);
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  if(millis() - lastReading >= 1000 || lastReading == 0) {
    lastReading = millis();
    if(isAutoSend) {
      if(useReading) {
        if(readResponse()) {
          Serial.print("PM 2.5: "); Serial.print(pm25);
          Serial.print(" / PM 10: "); Serial.println(pm10);
        }
      } else {
        sendCommand(AutoSendOff);
        if(readResponse()) {
          Serial.println("AutoSend disabled.");
          isAutoSend = !isAutoSend;
        }
      }
    } else {
      sendCommand(ReadPm);
      if(readResponse()) {
        Serial.print("PM 2.5: "); Serial.print(pm25);
        Serial.print(" / PM 10: "); Serial.println(pm10);
      }
    }
  }

  // Publish to MQTT every 30 seconds
  if (millis() - lastMqttPublish >= 30000) {
    lastMqttPublish = millis();
    char payload[64];
    snprintf(payload, sizeof(payload), "{\"pm25\": %d, \"pm10\": %d}", pm25, pm10);
    client.publish(mqtt_topic, payload);
    Serial.println("MQTT publish: ");
    Serial.println(payload);
  }
}
