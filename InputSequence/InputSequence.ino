#include <WiFiManager.h>
#include <PubSubClient.h>

const char* AP_NAME             = "Input Sequence Setup";             // Access point name
const char* DEFAULT_SERVER      = "192.168.1.10";                     // MQTT server name
const char* DEFAULT_SUB_TOPIC   = "puzzles/input-sequence/commands";  // MQTT sub topic
const char* DEFAULT_PUB_TOPIC   = "puzzles/input-sequence/status";    // MQTT pub topic

WiFiClient espClient;
PubSubClient client(espClient);

char mqtt_server[40];
char mqtt_sub_topic[100];
char mqtt_pub_topic[100];

#define NUM_BUTTONS 6
#define LOCK_PIN    13

const byte buttonPins[NUM_BUTTONS] = {23, 22, 21, 18, 5, 4};
const byte ledPins   [NUM_BUTTONS] = {27, 14, 32, 33, 25, 26};
const byte steps     [NUM_BUTTONS] = { 0, 1, 2, 3, 4, 5 };

int    currentStep      = 0;
bool   puzzleSolved     = false;
bool   lastInputState[NUM_BUTTONS] = { HIGH };

unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;

String clientId = "input-sequence-" + String(ESP.getChipRevision()) + "-" + String(random(0xffff), HEX);

void onSolve() {
  digitalWrite(LOCK_PIN, LOW);
  Serial.println("solved");
  puzzleSolved = true;
}

void resetPuzzle() {
  currentStep = 0;
  for (int i = 0; i < NUM_BUTTONS; i++) lastInputState[i] = HIGH;
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

  for (int i = 0; i < NUM_BUTTONS; i++) {
    pinMode(buttonPins[i], INPUT_PULLUP);
    pinMode(ledPins[i], OUTPUT);
    digitalWrite(ledPins[i], LOW);
  }
  pinMode(LOCK_PIN, OUTPUT);
  digitalWrite(LOCK_PIN, HIGH);

  setupNetworking();
}

void loop() {
  if (!client.connected()) {
    Serial.println("MQTT: Trying to connect...");
    client.connect(clientId.c_str(), NULL, NULL, NULL, 0, false, NULL, true);
    client.subscribe(mqtt_sub_topic);
  }
  client.loop();

  if (puzzleSolved) {
    for (int i = 0; i < NUM_BUTTONS; i++) {
      digitalWrite(ledPins[i], HIGH);
      delay(100);
      digitalWrite(ledPins[i], LOW);
    }
    return;
  }

  if (millis() - lastDebounceTime > debounceDelay) {
    for (int i = 0; i < NUM_BUTTONS; i++) {
      int reading = digitalRead(buttonPins[i]);
      if (reading != lastInputState[i]) {
        lastDebounceTime = millis();
      }
      if (reading == LOW && lastInputState[i] == HIGH) {
        if (steps[currentStep] == i) {
          currentStep++;
        } else {
          currentStep = 0;
        }
      }
      lastInputState[i] = reading;
    }
  }

  if (currentStep == NUM_BUTTONS) {
    onSolve();
    client.publish(mqtt_pub_topic, "solved");
  }

  for (int i = 0; i < NUM_BUTTONS; i++) {
    digitalWrite(ledPins[i], (i < currentStep ? HIGH : LOW));
  }
}
