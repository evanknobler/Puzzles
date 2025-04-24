#include <WiFiManager.h>
#include <PubSubClient.h>
#include <DFRobotDFPlayerMini.h>
#include <Preferences.h>

Preferences prefs;
bool shouldSaveConfig = false;
WiFiManagerParameter *custom_mqtt_server;
WiFiManagerParameter *custom_pub_topic;

WiFiClient espClient;
PubSubClient client(espClient);
bool mqttConnected = false;

char mqtt_server[40];
char mqtt_pub_topic[40];

#define SENSOR_PIN A3
#define THRESHOLD 2000

const char* AP_NAME = "Hold Hands Setup";

String clientId = "hold-hands-" + String(ESP.getChipRevision()) + "-" + String(random(0xffff), HEX);

enum DeviceState { INITIALISING, CIRCUIT_OPEN, CIRCUIT_CLOSED };
DeviceState deviceState = INITIALISING;

HardwareSerial hardwareSerial(2);
DFRobotDFPlayerMini dfPlayer;

void saveConfigCallback() {
  shouldSaveConfig = true;
}

void setup() {
  Serial.begin(115200);
  hardwareSerial.begin(9600, SERIAL_8N1, 16, 17);
  delay(500);
  if (!dfPlayer.begin(hardwareSerial)) while(true);
  dfPlayer.volume(30);

  deviceState = CIRCUIT_OPEN;
  for (int i = 0; i < 20; i++) {
    analogRead(SENSOR_PIN);
    delay(50);
  }

  prefs.begin("settings", false);
  String storedServer = prefs.getString("mqtt_server", "192.168.0.100");
  String storedPub    = prefs.getString("mqtt_pub",    "puzzles/hold-hands/status");

  custom_mqtt_server = new WiFiManagerParameter("server", "MQTT Server IP", storedServer.c_str(), 40);
  custom_pub_topic   = new WiFiManagerParameter("pub",    "Publish Topic",   storedPub.c_str(),    40);

  WiFiManager wm;
  wm.setSaveConfigCallback(saveConfigCallback);
  wm.addParameter(custom_mqtt_server);
  wm.addParameter(custom_pub_topic);
  wm.autoConnect(AP_NAME);

  if (shouldSaveConfig) {
    prefs.putString("mqtt_server", custom_mqtt_server->getValue());
    prefs.putString("mqtt_pub",    custom_pub_topic->getValue());
  }
  prefs.end();

  String ipStr  = custom_mqtt_server->getValue(); ipStr.toCharArray(mqtt_server, sizeof(mqtt_server));
  String pubStr = custom_pub_topic->getValue();   pubStr.toCharArray(mqtt_pub_topic, sizeof(mqtt_pub_topic));

  client.setServer(mqtt_server, 1883);

  unsigned long startAttempt = millis();
  while (!client.connected() && millis() - startAttempt < 5000) {
    if (client.connect(clientId)) mqttConnected = true;
    else delay(500);
  }
  if (!mqttConnected) {
    wm.resetSettings();
    wm.startConfigPortal(AP_NAME);
    startAttempt = millis();
    while (!client.connected() && millis() - startAttempt < 5000) {
      if (client.connect(clientId)) mqttConnected = true;
      else delay(500);
    }
  }
}

void loop() {
  if (mqttConnected) client.loop();

  int reading = analogRead(SENSOR_PIN);

  switch (deviceState) {
    case INITIALISING:
      break;

    case CIRCUIT_OPEN:
      if (reading < THRESHOLD) {
        dfPlayer.playMp3Folder(1);
        client.publish(mqtt_pub_topic, "solved");
        delay(100);
        deviceState = CIRCUIT_CLOSED;
      }
      break;

    case CIRCUIT_CLOSED:
      if (reading > THRESHOLD) {
        dfPlayer.stop();
        delay(100);
        deviceState = CIRCUIT_OPEN;
      }
      break;
  }
  delay(10);
}
