#include <WiFiManager.h>
#include <PubSubClient.h>
#include <DFRobotDFPlayerMini.h>
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

#define NUM_INPUTS 6
#define LOCK_PIN 4

HardwareSerial hardwareSerial(2);
DFRobotDFPlayerMini dfPlayer;

int currentStep = 0;
bool puzzleSolved = false;
const byte sensorPins[NUM_INPUTS] = {23, 22, 21, 19, 18, 5};
const byte correctSequence[NUM_INPUTS] = {0, 1, 2, 3, 4, 5};
bool lastInput[NUM_INPUTS] = {LOW};

const char* AP_NAME = "Audio Sequence Setup";

void saveConfigCallback() {
  shouldSaveConfig = true;
}

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

  prefs.begin("settings", false);
  String storedServer = prefs.getString("mqtt_server", "192.168.0.100");
  String storedSub    = prefs.getString("mqtt_sub",    "puzzles/audio-sequence/commands");
  String storedPub    = prefs.getString("mqtt_pub",    "puzzles/audio-sequence/status");

  custom_mqtt_server = new WiFiManagerParameter("server","MQTT Server IP",    storedServer.c_str(), 40);
  custom_sub_topic   = new WiFiManagerParameter("sub",   "Subscribe Topic",   storedSub.c_str(),    40);
  custom_pub_topic   = new WiFiManagerParameter("pub",   "Publish Topic",     storedPub.c_str(),    40);

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

  String ip  = custom_mqtt_server->getValue(); ip.toCharArray(mqtt_server,    sizeof(mqtt_server));
  String sub = custom_sub_topic->getValue();   sub.toCharArray(mqtt_sub_topic, sizeof(mqtt_sub_topic));
  String pub = custom_pub_topic->getValue();   pub.toCharArray(mqtt_pub_topic, sizeof(mqtt_pub_topic));

  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

  unsigned long startAttempt = millis();
  while (!client.connected() && millis() - startAttempt < 5000) {
    if (client.connect("esp32-client")) {
      client.subscribe(mqtt_sub_topic);
      mqttConnected = true;
    } else delay(500);
  }
  if (!mqttConnected) {
    wm.resetSettings();
    wm.startConfigPortal(AP_NAME);
    startAttempt = millis();
    while (!client.connected() && millis() - startAttempt < 5000) {
      if (client.connect("esp32-client")) {
        client.subscribe(mqtt_sub_topic);
        mqttConnected = true;
      } else delay(500);
    }
  }

  for (byte i = 0; i < NUM_INPUTS; i++) pinMode(sensorPins[i], INPUT);
}

void loop() {
  if (mqttConnected) {
    if (!client.connected()) {
      client.connect("esp32-client");
      client.subscribe(mqtt_sub_topic);
    }
    client.loop();
  }

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
