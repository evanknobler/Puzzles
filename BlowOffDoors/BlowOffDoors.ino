#include <WiFiManager.h>
#include <PubSubClient.h>
#include <DFRobotDFPlayerMini.h>

const char* AP_NAME             = "Blow Off Doors Setup";             // Access point name
const char* DEFAULT_SERVER      = "192.168.1.10";                     // MQTT server name
const char* DEFAULT_SUB_TOPIC   = "puzzles/blow-off-doors/commands";  // MQTT sub topic
const char* DEFAULT_PUB_TOPIC   = "puzzles/blow-off-doors/status";    // MQTT pub topic

WiFiClient espClient;
PubSubClient client(espClient);

char mqtt_server[40];
char mqtt_sub_topic[100];
char mqtt_pub_topic[100];

String clientId = "blow-off-doors-" + String(ESP.getChipRevision()) + "-" + String(random(0xffff), HEX);

HardwareSerial hardwareSerial(2);
DFRobotDFPlayerMini dfPlayer;
const byte relayOut = 22;
const byte relayIn  = 23;
bool lastRelayInState = HIGH;
bool puzzleSolved = false;

void onSolve() {
  digitalWrite(relayOut, HIGH);
  puzzleSolved = true;
}

void resetPuzzle() {
  digitalWrite(relayOut, LOW);
  puzzleSolved = false;
}

void callback(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  msg.trim();
  if (msg == "solved") {
    onSolve();
    dfPlayer.play(1);
  } else if (msg == "reset") {
    resetPuzzle();
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
  hardwareSerial.begin(9600, SERIAL_8N1, 16, 17);
  delay(500);
  if (!dfPlayer.begin(hardwareSerial)) {
    Serial.println("Failed to start DFPlayerMini.");
    while (true);
  }
  dfPlayer.volume(30);

  pinMode(relayIn, INPUT_PULLUP);
  pinMode(relayOut, OUTPUT);
  digitalWrite(relayOut, LOW);

  setupNetworking();
}

void loop() {
  if (!client.connected()) {
    Serial.println("MQTT: Trying to connect...");
    client.connect(clientId.c_str(), NULL, NULL, NULL, 0, false, NULL, true);
    client.subscribe(mqtt_sub_topic);
  }
  client.loop();

  bool relayInState = digitalRead(relayIn);
  if (!puzzleSolved && relayInState != lastRelayInState) {
    if (relayInState == LOW) {
      dfPlayer.play(1);
      client.publish(mqtt_pub_topic, "solved");
      onSolve();
    }
    lastRelayInState = relayInState;
  }
}
