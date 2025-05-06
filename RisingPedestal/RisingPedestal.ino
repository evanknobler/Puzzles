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

String clientId = "rising-pedestal-" + String(ESP.getChipRevision()) + "-" + String(random(0xffff), HEX);

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

void setupNetworking() {
  prefs.begin("settings", false);
  String defaultServer = prefs.getString("mqtt_server", "192.168.1.10");
  String defaultSub    = prefs.getString("mqtt_sub",    "puzzles/rising-pedestal/commands");
  String defaultPub    = prefs.getString("mqtt_pub",    "puzzles/rising-pedestal/status");

  custom_mqtt_server = new WiFiManagerParameter("server", "MQTT Server IP", defaultServer.c_str(), 40);
  custom_sub_topic   = new WiFiManagerParameter("sub",    "Subscribe Topic", defaultSub.c_str(),    40);
  custom_pub_topic   = new WiFiManagerParameter("pub",    "Publish Topic",   defaultPub.c_str(),    40);

  WiFiManager wm;
  wm.setSaveConfigCallback(saveConfigCallback);
  wm.addParameter(custom_mqtt_server);
  wm.addParameter(custom_sub_topic);
  wm.addParameter(custom_pub_topic);

  wm.autoConnect(AP_NAME);

  if (shouldSaveConfig) {
    String newServer = custom_mqtt_server->getValue();
    String newSub    = custom_sub_topic->getValue();
    String newPub    = custom_pub_topic->getValue();

    Serial.println("Saving MQTT config values...");
    Serial.print("Server: "); Serial.println(newServer);
    Serial.print("Sub: ");    Serial.println(newSub);
    Serial.print("Pub: ");    Serial.println(newPub);

    prefs.putString("mqtt_server", newServer);
    prefs.putString("mqtt_sub",    newSub);
    prefs.putString("mqtt_pub",    newPub);
    prefs.end();

    newServer.toCharArray(mqtt_server, sizeof(mqtt_server));
    newSub.toCharArray(mqtt_sub_topic, sizeof(mqtt_sub_topic));
    newPub.toCharArray(mqtt_pub_topic, sizeof(mqtt_pub_topic));
  } else {
    defaultServer.toCharArray(mqtt_server, sizeof(mqtt_server));
    defaultSub.toCharArray(mqtt_sub_topic, sizeof(mqtt_sub_topic));
    defaultPub.toCharArray(mqtt_pub_topic, sizeof(mqtt_pub_topic));
  }

  Serial.print("Final MQTT server being used: ");
  Serial.println(mqtt_server);

  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

  unsigned long startAttempt = millis();
  while (!client.connected() && millis() - startAttempt < 5000) {
    Serial.print("Attempting MQTT connection to: ");
    Serial.println(mqtt_server);
    if (client.connect(clientId.c_str())) {
      Serial.println("MQTT connected successfully!");
      client.subscribe(mqtt_sub_topic);
      mqttConnected = true;
    } else {
      Serial.print("MQTT connection failed, state: ");
      Serial.println(client.state());
      delay(500);
    }
  }

  if (!mqttConnected) {
    Serial.println("Initial MQTT connection failed. Resetting WiFiManager config...");
    wm.resetSettings();
    wm.startConfigPortal(AP_NAME);

    startAttempt = millis();
    while (!client.connected() && millis() - startAttempt < 5000) {
      Serial.print("Attempting MQTT connection to: ");
      Serial.println(mqtt_server);
      if (client.connect(clientId.c_str())) {
        Serial.println("MQTT connected successfully!");
        client.subscribe(mqtt_sub_topic);
        mqttConnected = true;
      } else {
        Serial.print("MQTT connection failed, state: ");
        Serial.println(client.state());
        delay(500);
      }
    }
  }
}

void setup() {
  Serial.begin(115200);
  for (int i = 0; i < 2; i++) {
    pinMode(relayPins[i], OUTPUT);
    digitalWrite(relayPins[i], LOW);
  }

  setupNetworking();
}

void loop() {
  if (mqttConnected) {
    if (!client.connected()) {
      client.connect(clientId.c_str());
      client.subscribe(mqtt_sub_topic);
    }
    client.loop();
  }
}
