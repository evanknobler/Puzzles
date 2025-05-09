#include <WiFiManager.h>
#include <PubSubClient.h>
#include <DFRobotDFPlayerMini.h>

const char* AP_NAME             = "Blow Out Candles Setup";             // Access point name
const char* DEFAULT_SERVER      = "192.168.1.10";                       // MQTT server name
const char* DEFAULT_SUB_TOPIC   = "puzzles/blow-out-candles/commands";  // MQTT sub topic
const char* DEFAULT_PUB_TOPIC   = "puzzles/blow-out-candles/status";    // MQTT pub topic

WiFiClient espClient;
PubSubClient client(espClient);

char mqtt_server[40];
char mqtt_sub_topic[100];
char mqtt_pub_topic[100];

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

String clientId = "blow-out-candles-" + String(ESP.getChipRevision()) + "-" + String(random(0xffff), HEX);

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
  hardwareSerial.begin(9600, SERIAL_8N1, 34, 14);
  delay(500);
  if (!dfPlayer.begin(hardwareSerial)) {
    Serial.println("Failed to start DFPlayerMini");
    while(true);
  }
  dfPlayer.volume(30);

  for(int i=0; i<numCandles; i++) pinMode(ledPins[i], OUTPUT);
  for(int i=0; i<4; i++) pinMode(muxPins[i], OUTPUT);
  pinMode(inputPin, INPUT);
  pinMode(lockPin, OUTPUT);
  digitalWrite(lockPin, HIGH);
  resetPuzzle();

  setupNetworking();
}

void loop() {
  if (!client.connected()) {
    Serial.println("MQTT: Trying to connect...");
    client.connect(clientId.c_str(), NULL, NULL, NULL, 0, false, NULL, true);
    client.subscribe(mqtt_sub_topic);
  }
  client.loop();
  
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
