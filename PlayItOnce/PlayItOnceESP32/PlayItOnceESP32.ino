#include <WiFiManager.h>
#include <PubSubClient.h>
#include <Preferences.h>
#include <FastLED.h>
#include "pitches.h"

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

const byte numSteps = 9;
const byte melody[numSteps] = {50, 55, 58, 57, 55, 62, 61, 58, 55};
const byte lockPin = 22;
const byte audioOutPin = 23;
int currentStep = 0;
static uint8_t gHue = 0;
const char* AP_NAME = "Play It Once Setup";

String clientId = "play-it-once-" + String(ESP.getChipRevision()) + "-" + String(random(0xffff), HEX);

#define LEDS_PIN 2
CRGB leds[numSteps];
#define LED_STRIP_PIN 18
CRGB ledStrip[300];
enum PuzzleState { Initialising, Running, Solved };
PuzzleState puzzleState = Initialising;

String serialBuffer = "";

void saveConfigCallback() {
  shouldSaveConfig = true;
}

void callback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (unsigned int i = 0; i < length; i++) message += (char)payload[i];
  if (message == "solved") onSolve();
  else if (message == "reset") onReset();
}

void onSolve() {
  digitalWrite(lockPin, LOW);
  puzzleState = Solved;
  delay(100);
  noTone(audioOutPin);
}

void onReset() {
  noTone(audioOutPin);
  for (int i = 0; i < numSteps; i++) {
    leds[i] = CRGB::Yellow;
    FastLED.show();
    delay(20);
    leds[i] = CRGB::Black;
  }
  FastLED.clear();
  digitalWrite(lockPin, HIGH);
  puzzleState = Running;
  currentStep = 0;
}

void processMidiCommand(const String &cmd) {
  int c1 = cmd.indexOf(','), c2 = cmd.indexOf(',', c1 + 1), c3 = cmd.indexOf(',', c2 + 1);
  if (c1 < 0 || c2 < 0 || c3 < 0) return;
  String type = cmd.substring(0, c1);
  byte pitch = cmd.substring(c2 + 1, c3).toInt();
  if (type == "NOTE_ON") {
    tone(audioOutPin, sNotePitches[pitch]);
    if (puzzleState == Running) {
      if (pitch == melody[currentStep]) {
        currentStep++;
        if (currentStep == numSteps) {
          onSolve();
          client.publish(mqtt_pub_topic, "solved");
          currentStep = 0;
        }
      } else {
        for (int i = currentStep; i < numSteps; i++) {
          leds[i] = CRGB::Red;
          FastLED.show();
          delay(20);
        }
        FastLED.clear();
        currentStep = 0;
      }
    }
  } else if (type == "NOTE_OFF") {
    noTone(audioOutPin);
  }
}

void updateDisplay() {
  switch (puzzleState) {
    case Running:
      for (int i = 0; i < numSteps; i++) {
        leds[i] = (i < currentStep ? CRGB::Green : CRGB::Black);
      }
      break;

    case Solved:
      fadeToBlackBy(leds, numSteps, 20);
      {
        uint16_t pos = beatsin16(12, 0, numSteps - 1);
        leds[pos] = CRGB::Blue;
      }
      fill_rainbow(ledStrip, 300, gHue, 7);
      gHue++;
      FastLED.delay(30);
      break;
  }
  FastLED.show();
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(115200, SERIAL_8N1, 16, 17);

  prefs.begin("settings", false);
  String storedServer = prefs.getString("mqtt_server", "192.168.0.100");
  String storedSub    = prefs.getString("mqtt_sub",    "puzzles/play-it-once/commands");
  String storedPub    = prefs.getString("mqtt_pub",    "puzzles/play-it-once/status");

  custom_mqtt_server = new WiFiManagerParameter("server", "MQTT Server IP",  storedServer.c_str(), 40);
  custom_sub_topic   = new WiFiManagerParameter("sub",    "Subscribe Topic",   storedSub.c_str(),    40);
  custom_pub_topic   = new WiFiManagerParameter("pub",    "Publish Topic",     storedPub.c_str(),    40);

  FastLED.addLeds<WS2812B, LEDS_PIN, GRB>(leds, numSteps);
  FastLED.addLeds<WS2812B, LED_STRIP_PIN, GRB>(ledStrip, 300);
  pinMode(lockPin, OUTPUT);
  digitalWrite(lockPin, HIGH);
  puzzleState = Running;

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
  if (mqttConnected && !client.connected()) {
    client.connect(clientId.c_str());
    client.subscribe(mqtt_sub_topic);
  }
  if (mqttConnected) client.loop();

  while (Serial2.available()) {
    char ch = (char)Serial2.read();
    if (ch == '\n') {
      processMidiCommand(serialBuffer);
      serialBuffer = "";
    } else serialBuffer += ch;
  }

  updateDisplay();
}
