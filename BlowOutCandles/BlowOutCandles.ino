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

const byte numCandles = 12;
const byte ledPins[numCandles] = { 26, 27, 2, 0, 18, 32, 33, 25, 23, 22, 21, 19 };
const byte muxPins[4] = {5,17,16,4};
const byte inputPin = 36;
const byte order[numCandles] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
const byte lockPin = 15;
bool puzzleSolved  = false;

HardwareSerial hardwareSerial(2);
DFRobotDFPlayerMini dfPlayer;
int currentStep = 0;
bool isLit[numCandles];
int mistakesMade = 0;
const char* AP_NAME = "Blow Out Candles Setup";

String clientId = "blow-out-candles-" + String(ESP.getChipRevision()) + "-" + String(random(0xffff), HEX);

void saveConfigCallback() {
  shouldSaveConfig = true;
}

void callback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (unsigned int i=0; i<length; i++) message += (char)payload[i];
  if (message == "solved") onSolve();
  else if (message == "reset") resetPuzzle();
}

bool readInput(byte micNum) {
  for (int i=0; i<4; i++) {
    digitalWrite(muxPins[i], (micNum >> i) & 0x01);
    delayMicroseconds(10);
  }
  return digitalRead(inputPin);
}

void resetPuzzle() {
  puzzleSolved = false;
  dfPlayer.stop();
  for(int i=0; i<numCandles; i++) {
    isLit[i] = true;
    digitalWrite(ledPins[i], HIGH);
    delay(100);
  }
  currentStep = 0;
  mistakesMade = 0;
  digitalWrite(lockPin, HIGH);
}

void onSolve() {
  Serial.println("solved");
  puzzleSolved = true;
  for(int i=0; i<numCandles; i++) {
    digitalWrite(ledPins[i], LOW);
  }
  dfPlayer.play(1);
  digitalWrite(lockPin, LOW);
}

void setup() {
  Serial.begin(115200);
  hardwareSerial.begin(9600, SERIAL_8N1, 34, 14);
  delay(500);
  if (!dfPlayer.begin(hardwareSerial)) while(true);
  dfPlayer.volume(30);

  for(int i=0; i<numCandles; i++) pinMode(ledPins[i], OUTPUT);
  for(int i=0; i<4; i++) pinMode(muxPins[i], OUTPUT);
  pinMode(inputPin, INPUT);
  pinMode(lockPin, OUTPUT);
  digitalWrite(lockPin, HIGH);
  resetPuzzle();

  prefs.begin("settings", false);
  String storedServer = prefs.getString("mqtt_server", "192.168.0.100");
  String storedSub    = prefs.getString("mqtt_sub",    "puzzles/blow-out-candles/commands");
  String storedPub    = prefs.getString("mqtt_pub",    "puzzles/blow-out-candles/status");

  custom_mqtt_server = new WiFiManagerParameter("server", "MQTT Server IP", storedServer.c_str(), 40);
  custom_sub_topic   = new WiFiManagerParameter("sub",    "Subscribe Topic",   storedSub.c_str(),    40);
  custom_pub_topic   = new WiFiManagerParameter("pub",    "Publish Topic",     storedPub.c_str(),    40);

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

  String ipStr  = custom_mqtt_server->getValue(); ipStr.toCharArray(mqtt_server, sizeof(mqtt_server));
  String subStr = custom_sub_topic->getValue();   subStr.toCharArray(mqtt_sub_topic, sizeof(mqtt_sub_topic));
  String pubStr = custom_pub_topic->getValue();   pubStr.toCharArray(mqtt_pub_topic, sizeof(mqtt_pub_topic));

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
  
  if (!puzzleSolved) {
    for (int i=0; i<numCandles; i++) {
      if (isLit[i]) {
        //analogWrite(ledPins[i], random(128,255));
        if (readInput(i) == HIGH) {
          delay(20);
          if (readInput(i) == HIGH) {
            isLit[i] = false;
            digitalWrite(ledPins[i], LOW);
            if (order[currentStep] != i) mistakesMade++;
            currentStep++;
          }
        }
      }
    }

    if (currentStep == numCandles) {
      if (mistakesMade > 0) {
        delay(2000);
        resetPuzzle();
      } else {
        onSolve();
        client.publish(mqtt_pub_topic, "solved");
      }
    }
  }
}
