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

#define NUM_SENSORS 4
#define RELAY_PIN 32
const char* AP_NAME = "Knife Puzzle Setup";

String clientId = "knife-throwing-" + String(ESP.getChipRevision()) + "-" + String(random(0xffff), HEX);

const byte sensorPins[NUM_SENSORS] = {21, 19, 18, 5};
bool lastSensorState[NUM_SENSORS];

HardwareSerial hardwareSerial(2);
DFRobotDFPlayerMini dfPlayer;
enum State { Initialising, Running, Solved };
State state = Initialising;

void saveConfigCallback() {
  shouldSaveConfig = true;
}

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

  prefs.begin("settings", false);
  String sSrv = prefs.getString("mqtt_server", "192.168.0.100");
  String sSub = prefs.getString("mqtt_sub",    "puzzles/knife-throwing/commands");
  String sPub = prefs.getString("mqtt_pub",    "puzzles/knife-throwing/status");

  custom_mqtt_server = new WiFiManagerParameter("server","MQTT Server IP", sSrv.c_str(), 40);
  custom_sub_topic   = new WiFiManagerParameter("sub","Subscribe Topic",  sSub.c_str(),  40);
  custom_pub_topic   = new WiFiManagerParameter("pub","Publish Topic",    sPub.c_str(),  40);

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

  String ip = custom_mqtt_server->getValue(); ip.toCharArray(mqtt_server, sizeof(mqtt_server));
  String sub = custom_sub_topic->getValue();    sub.toCharArray(mqtt_sub_topic, sizeof(mqtt_sub_topic));
  String pub = custom_pub_topic->getValue();    pub.toCharArray(mqtt_pub_topic, sizeof(mqtt_pub_topic));

  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

  unsigned long startAttempt = millis();
  while (!client.connected() && millis() - startAttempt < 5000) {
    if (client.connect(clientId.c_str())) {
      client.subscribe(mqtt_sub_topic);
      mqttConnected = true;
    } else delay(500);
  }
  if (!mqttConnected) {
    wm.resetSettings();
    wm.startConfigPortal(AP_NAME);
    startAttempt = millis();
    while (!client.connected() && millis() - startAttempt < 5000) {
      if (client.connect(clientId.c_str())) {
        client.subscribe(mqtt_sub_topic);
        mqttConnected = true;
      } else delay(500);
    }
  }
}

void loop() {
  if (mqttConnected) {
    if (!client.connected()) {
      client.connect(clientId.c_str());
      client.subscribe(mqtt_sub_topic);
    }
    client.loop();
  }

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
