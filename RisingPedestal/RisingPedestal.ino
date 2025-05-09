#include <WiFiManager.h>
#include <PubSubClient.h>

const char* AP_NAME             = "Rising Pedestal Setup";                 // Access point name
const char* DEFAULT_SERVER      = "192.168.1.10";                          // MQTT server name
const char* DEFAULT_SUB_TOPIC   = "puzzles/rising-pedestal/commands";      // MQTT sub topic
const char* DEFAULT_PUB_TOPIC   = "puzzles/rising-pedestal/status";        // MQTT pub topic

WiFiClient espClient;
PubSubClient client(espClient);

char mqtt_server[40];
char mqtt_sub_topic[40];
char mqtt_pub_topic[40];

String clientId = "rising-pedestal-" + String(ESP.getChipRevision()) + "-" + String(random(0xffff), HEX);

// Relay Outputs
const byte relayPins[2] = {23, 22};

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
  WiFiManager wm;
  wm.setConfigPortalTimeout(180);
  
  WiFiManagerParameter custom_mqtt_server("server", "MQTT Server IP", DEFAULT_SERVER, 40);
  WiFiManagerParameter custom_sub_topic("sub", "MQTT Sub Topic", DEFAULT_SUB_TOPIC, 100);
  WiFiManagerParameter custom_pub_topic("pub", "MQTT Pub Topic", DEFAULT_PUB_TOPIC, 100);
  wm.addParameter(&custom_mqtt_server);
  wm.addParameter(&custom_sub_topic);
  wm.addParameter(&custom_pub_topic);

  bool res = wm.startConfigPortal(AP_NAME);
  String mqttServer;
  String mqttSub;
  String mqttPub;

  if (res) {
    Serial.println("Connected to Network");

    mqttServer = custom_mqtt_server.getValue();
    mqttSub = custom_sub_topic.getValue();
    mqttPub = custom_pub_topic.getValue();

    Serial.print("MQTT Server IP: ");
    Serial.println(mqttServer);
    Serial.print("MQTT Sub Topic: ");
    Serial.println(mqttSub);
    Serial.print("MQTT Pub Topic: ");
    Serial.println(mqttPub);
  }
  else {
    Serial.println("Config Portal Timeout");
    Serial.println("Restarting...");
    ESP.restart();
  }

  mqttServer.toCharArray(mqtt_server, sizeof(mqtt_server));
  mqttSub.toCharArray(mqtt_sub_topic, sizeof(mqtt_sub_topic));
  mqttPub.toCharArray(mqtt_pub_topic, sizeof(mqtt_pub_topic));

  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
  client.connect(clientId.c_str(), NULL, NULL, NULL, 0, false, NULL, true);
  client.subscribe(mqtt_sub_topic);
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
  if (!client.connected()) {
    Serial.println("MQTT: Trying to connect...");
    client.connect(clientId.c_str(), NULL, NULL, NULL, 0, false, NULL, true);
    client.subscribe(mqtt_sub_topic);
  }
  client.loop();
}
