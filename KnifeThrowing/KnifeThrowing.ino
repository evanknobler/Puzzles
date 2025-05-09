#include <WiFiManager.h>
#include <PubSubClient.h>
#include <DFRobotDFPlayerMini.h>

const char* AP_NAME             = "Knife Puzzle Setup";                 // Access point name
const char* DEFAULT_SERVER      = "192.168.1.10";                       // MQTT server name
const char* DEFAULT_SUB_TOPIC   = "puzzles/knife-throwing/commands";    // MQTT sub topic
const char* DEFAULT_PUB_TOPIC   = "puzzles/knife-throwing/status";      // MQTT pub topic

WiFiClient espClient;
PubSubClient client(espClient);

char mqtt_server[40];
char mqtt_sub_topic[100];
char mqtt_pub_topic[100];

#define NUM_SENSORS 4
#define RELAY_PIN 32

String clientId = "knife-throwing-" + String(ESP.getChipRevision()) + "-" + String(random(0xffff), HEX);

const byte sensorPins[NUM_SENSORS] = {21, 19, 18, 5};
bool lastSensorState[NUM_SENSORS];

HardwareSerial hardwareSerial(2);
DFRobotDFPlayerMini dfPlayer;
enum State { Initialising, Running, Solved };
State state = Initialising;

void onSolve() {
  Serial.println("Solved");
  dfPlayer.play(1);
  digitalWrite(RELAY_PIN, LOW);
  state = Solved;
}

void resetPuzzle() {
  dfPlayer.stop();
  digitalWrite(RELAY_PIN, HIGH);
  state = Running;
}

void callback(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  msg.trim();
  if (msg == "solved") {
    onSolve();
  } else if (msg == "reset") {
    resetPuzzle();
  }
}

void printState() {
  for (int i = 0; i < NUM_SENSORS; i++) {
    Serial.print(lastSensorState[i]);
    Serial.print(i < NUM_SENSORS - 1 ? ',' : '\n');
  }
}

bool isSolved() {
  for (int i = 0; i < NUM_SENSORS; i++) {
    if (lastSensorState[i] == HIGH) return false;
  }
  return true;
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
  if (!dfPlayer.begin(hardwareSerial)) while (true);
  dfPlayer.volume(30);

  for (int i = 0; i < NUM_SENSORS; i++) {
    pinMode(sensorPins[i], INPUT);
    lastSensorState[i] = digitalRead(sensorPins[i]);
  }
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH);
  state = Running;

  setupNetworking();
}

void loop() {
  if (!client.connected()) {
    Serial.println("MQTT: Trying to connect...");
    client.connect(clientId.c_str(), NULL, NULL, NULL, 0, false, NULL, true);
    client.subscribe(mqtt_sub_topic);
  }
  client.loop();

  for (int i = 0; i < NUM_SENSORS; i++) {
    bool st = digitalRead(sensorPins[i]);
    if (st != lastSensorState[i]) {
      lastSensorState[i] = st;
      printState();
      if (state == Running && isSolved()) {
        onSolve();
        client.publish(mqtt_pub_topic, "solved");
      } else if (state == Solved && !isSolved()) {
        resetPuzzle();
      }
    }
  }
}
