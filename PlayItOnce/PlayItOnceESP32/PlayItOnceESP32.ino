#include <WiFiManager.h>
#include <PubSubClient.h>
#include <FastLED.h>
#include "pitches.h"

const char* AP_NAME             = "Play It Once Setup";                 // Access point name
const char* DEFAULT_SERVER      = "192.168.1.10";                       // MQTT server name
const char* DEFAULT_SUB_TOPIC   = "puzzles/play-it-once/commands";      // MQTT sub topic
const char* DEFAULT_PUB_TOPIC   = "puzzles/play-it-once/status";        // MQTT pub topic

WiFiClient espClient;
PubSubClient client(espClient);

char mqtt_server[40];
char mqtt_sub_topic[100];
char mqtt_pub_topic[100];

const byte numSteps = 9;
const byte melody[numSteps] = {50, 55, 58, 57, 55, 62, 61, 58, 55};
const byte lockPin = 22;
const byte audioOutPin = 23;
int currentStep = 0;
static uint8_t gHue = 0;
uint8_t ledStripBrightness = 128;

String clientId = "play-it-once-" + String(ESP.getChipRevision()) + "-" + String(random(0xffff), HEX);

#define LEDS_PIN 2
CRGB leds[numSteps];
#define LED_STRIP_PIN 18
CRGB ledStrip[300];
enum PuzzleState { Initialising, Running, Solved };
PuzzleState puzzleState = Initialising;

String serialBuffer = "";

void callback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (unsigned int i = 0; i < length; i++) message += (char)payload[i];
  Serial.println(message);
  if (message == "solved") onSolve();
  else if (message == "reset") onReset();
  else if (message.startsWith("brightness:")) {
    int value = message.substring(11).toInt();
    value = constrain(value, 0, 100);
    ledStripBrightness = map(value, 0, 10, 0, 255);
    Serial.print("Updated brightness: ");
    Serial.println(ledStripBrightness);
  }
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

      for (int i = 0; i < 300; i++) {
        ledStripBrightness = constrain(ledStripBrightness, 0, 255);
        ledStrip[i] = CRGB(ledStripBrightness, ledStripBrightness * 0.85, ledStripBrightness * 0.5);  // warm white/yellow
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
  Serial2.begin(115200, SERIAL_8N1, 16, 17);

  FastLED.addLeds<WS2812B, LEDS_PIN, GRB>(leds, numSteps);
  FastLED.addLeds<WS2812B, LED_STRIP_PIN, GRB>(ledStrip, 300);
  pinMode(lockPin, OUTPUT);
  digitalWrite(lockPin, HIGH);
  puzzleState = Running;

  setupNetworking();
}

void loop() {
  if (!client.connected()) {
    Serial.println("MQTT: Trying to connect...");
    client.connect(clientId.c_str(), NULL, NULL, NULL, 0, false, NULL, true);
    client.subscribe(mqtt_sub_topic);
  }
  client.loop();

  while (Serial2.available()) {
    char ch = (char)Serial2.read();
    if (ch == '\n') {
      processMidiCommand(serialBuffer);
      serialBuffer = "";
    } else serialBuffer += ch;
  }

  updateDisplay();
}
