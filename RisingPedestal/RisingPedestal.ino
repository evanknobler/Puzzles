#include <WiFiManager.h>
#include <PubSubClient.h>
#include <Preferences.h>

Preferences prefs;
bool shouldSaveConfig = false;
WiFiManagerParameter *custom_mqtt_server;
WiFiManagerParameter *custom_sub_topic;
WiFiManagerParameter *custom_pub_topic;

WiFiClient espClient;
PubSubClient client(espClient);
bool mqttConnected = false;

char mqtt_server[40];
char mqtt_sub_topic[40];
char mqtt_pub_topic[40];

const char* AP_NAME = "Rising Pedestal Setup";

// Relay Outputs
const byte relayPins[2] = {23, 22};

void saveConfigCallback() {
  shouldSaveConfig = true;
}

void rise() {
  digitalWrite(relayPins[0], HIGH);
  digitalWrite(relayPins[1], LOW);
}

void fall() {
  digitalWrite(relayPins[0], LOW);
  digitalWrite(relayPins[1], HIGH);
}

void stopActuator() {
  digitalWrite(relayPins[0], LOW);
  digitalWrite(relayPins[1], LOW);
}

void callback(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  msg.trim();
  if (msg == "solved") {
    rise();
  } else if (msg == "reset") {
    fall();
  }
}

void setup() {
  Serial.begin(115200);
  for (int i = 0; i < 2; i++) {
    pinMode(relayPins[i], OUTPUT);
    digitalWrite(relayPins[i], LOW);
  }

  prefs.begin("settings", false);
  String srv = prefs.getString("mqtt_server", "192.168.0.100");
  String sub = prefs.getString("mqtt_sub",    "puzzles/rising-pedestal/commands");
  String pub = prefs.getString("mqtt_pub",    "puzzles/rising-pedestal/status");

  custom_mqtt_server = new WiFiManagerParameter("server","MQTT Server IP",       srv.c_str(), 40);
  custom_sub_topic   = new WiFiManagerParameter("sub",   "Subscribe Topic",      sub.c_str(), 40);
  custom_pub_topic   = new WiFiManagerParameter("pub",   "Publish Topic",        pub.c_str(), 40);

  WiFiManager wm;
  wm.setSaveConfigCallback(saveConfigCallback);
  wm.addParameter(custom_mqtt_server);
  wm.addParameter(custom_sub_topic);
  wm.addParameter(custom_pub_topic);
  wm.autoConnect(AP_NAME);

  if (shouldSaveConfig) {
    prefs.putString("mqtt_server", custom_mqtt_server->getValue());
    prefs.putString("mqtt_sub",    custom_sub_topic->getValue());
    prefs.putString("mqtt_pub",    custom_pub_topic->getValue());
  }
  prefs.end();

  String ip = custom_mqtt_server->getValue();   ip.toCharArray(mqtt_server,    sizeof(mqtt_server));
  String st = custom_sub_topic->getValue();    st.toCharArray(mqtt_sub_topic, sizeof(mqtt_sub_topic));
  String pt = custom_pub_topic->getValue();    pt.toCharArray(mqtt_pub_topic, sizeof(mqtt_pub_topic));

  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

  unsigned long start = millis();
  while (!client.connected() && millis() - start < 5000) {
    if (client.connect("esp32-client")) {
      client.subscribe(mqtt_sub_topic);
      mqttConnected = true;
    } else {
      delay(500);
    }
  }
  if (!mqttConnected) {
    wm.resetSettings();
    wm.startConfigPortal(AP_NAME);
    start = millis();
    while (!client.connected() && millis() - start < 5000) {
      if (client.connect("esp32-client")) {
        client.subscribe(mqtt_sub_topic);
        mqttConnected = true;
      } else {
        delay(500);
      }
    }
  }
}

void loop() {
  if (mqttConnected) {
    if (!client.connected()) {
      client.connect("esp32-client");
      client.subscribe(mqtt_sub_topic);
    }
    client.loop();
  }
}
