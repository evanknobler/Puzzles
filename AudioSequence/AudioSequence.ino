#include <WiFiManager.h>
#include <PubSubClient.h>
#include <DFRobotDFPlayerMini.h>

const char* AP_NAME             = "Audio Sequence Setup";             // Access point name
const char* DEFAULT_SERVER      = "192.168.1.10";                     // MQTT server name
const char* DEFAULT_SUB_TOPIC   = "puzzles/audio-sequence/commands";  // MQTT sub topic
const char* DEFAULT_PUB_TOPIC   = "puzzles/audio-sequence/status";    // MQTT pub topic

WiFiClient espClient;
PubSubClient client(espClient);

char mqtt_server[40];
char mqtt_sub_topic[100];
char mqtt_pub_topic[100];

#define NUM_INPUTS 6
#define LOCK_PIN 4

HardwareSerial hardwareSerial(2);
DFRobotDFPlayerMini dfPlayer;

int currentStep = 0;
bool puzzleSolved = false;
const byte sensorPins[NUM_INPUTS] = {23, 22, 21, 19, 18, 5};
const byte correctSequence[NUM_INPUTS] = {0, 1, 2, 3, 4, 5};
bool lastInput[NUM_INPUTS] = {LOW};

String clientId = "audio-sequence-" + String(ESP.getChipRevision()) + "-" + String(random(0xffff), HEX);

void onSolve() {
  Serial.println("solved");
  digitalWrite(LOCK_PIN, LOW);
  puzzleSolved = true;
}

void resetPuzzle() {
  dfPlayer.stop();
  currentStep = 0;
  for (int i = 0; i < NUM_INPUTS; i++) lastInput[i] = LOW;
  puzzleSolved = false;
  digitalWrite(LOCK_PIN, HIGH);
}

void callback(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  msg.trim();
  if (msg == "solved") onSolve();
  else if (msg == "reset") resetPuzzle();
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

  pinMode(LOCK_PIN, OUTPUT);
  digitalWrite(LOCK_PIN, HIGH);

  setupNetworking();

  // Step 7: Sensor pins
  for (byte i = 0; i < NUM_INPUTS; i++) pinMode(sensorPins[i], INPUT);
}

void loop() {
  if (!client.connected()) {
    Serial.println("MQTT: Trying to connect...");
    client.connect(clientId.c_str(), NULL, NULL, NULL, 0, false, NULL, true);
    client.subscribe(mqtt_sub_topic);
  }
  client.loop();

  if (puzzleSolved) {
    delay(10);
    return;
  }

  for (byte i = 0; i < NUM_INPUTS; i++) {
    bool currentReading = digitalRead(sensorPins[i]);
    if (!lastInput[i] && currentReading) {
      dfPlayer.play(i + 1);
      if (correctSequence[currentStep] == i) {
        currentStep++;
        if (currentStep == NUM_INPUTS) {
          onSolve();
          client.publish(mqtt_pub_topic, "solved");
        }
      } else {
        currentStep = 0;
      }
    }
    lastInput[i] = currentReading;
  }
  delay(10);
}
