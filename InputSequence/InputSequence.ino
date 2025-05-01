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

const char* AP_NAME = "Input Sequence Setup";

String clientId = "input-sequence-" + String(ESP.getChipRevision()) + "-" + String(random(0xffff), HEX);

void saveConfigCallback() {
  shouldSaveConfig = true;
}

void onSolve() {
  digitalWrite(LOCK_PIN, LOW);
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
  // Step 1: Initialize preferences
  prefs.begin("settings", false);
  String defaultServer = prefs.getString("mqtt_server", "192.168.0.100");
  String defaultSub    = prefs.getString("mqtt_sub",    "puzzles/input-sequence/commands");
  String defaultPub    = prefs.getString("mqtt_pub",    "puzzles/input-sequence/status");

  // Step 2: Initialize WiFiManager parameters with stored values
  custom_mqtt_server = new WiFiManagerParameter("server", "MQTT Server IP", defaultServer.c_str(), 40);
  custom_sub_topic   = new WiFiManagerParameter("sub",    "Subscribe Topic", defaultSub.c_str(),    40);
  custom_pub_topic   = new WiFiManagerParameter("pub",    "Publish Topic",   defaultPub.c_str(),    40);

  WiFiManager wm;
  wm.addParameter(custom_mqtt_server);
  wm.addParameter(custom_sub_topic);
  wm.addParameter(custom_pub_topic);

  // Step 3: Start config portal or auto connect
  wm.autoConnect(AP_NAME);

  // Step 4: Extract updated values from portal and save to preferences
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

  // Step 5: Convert to char arrays for PubSubClient
  newServer.toCharArray(mqtt_server, sizeof(mqtt_server));
  newSub.toCharArray(mqtt_sub_topic, sizeof(mqtt_sub_topic));
  newPub.toCharArray(mqtt_pub_topic, sizeof(mqtt_pub_topic));

  Serial.print("Final MQTT server being used: ");
  Serial.println(mqtt_server);

  // Step 6: MQTT setup
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
  if (mqttConnected) {
    if (!client.connected()) {
      client.connect(clientId.c_str());
      client.subscribe(mqtt_sub_topic);
    }
    client.loop();
  }

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
