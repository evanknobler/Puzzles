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

const char* AP_NAME = "Blow Off Doors Setup";

HardwareSerial hardwareSerial(2);
DFRobotDFPlayerMini dfPlayer;
const byte relayOut = 22;
const byte relayIn  = 23;
bool lastRelayInState = HIGH;
bool puzzleSolved = false;

void saveConfigCallback() {
  shouldSaveConfig = true;
}

void onSolve() {
  digitalWrite(relayOut, HIGH);
  puzzleSolved = true;
}

void resetPuzzle() {
  digitalWrite(relayOut, LOW);
  puzzleSolved = false;
}

void callback(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  msg.trim();
  if (msg == "solved") {
    onSolve();
    dfPlayer.play(1);
  } else if (msg == "reset") {
    resetPuzzle();
  }
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

  pinMode(relayIn, INPUT_PULLUP);
  pinMode(relayOut, OUTPUT);
  digitalWrite(relayOut, LOW);

  prefs.begin("settings", false);
  String storedServer = prefs.getString("mqtt_server", "192.168.0.100");
  String storedSub    = prefs.getString("mqtt_sub",    "puzzles/blow-off-doors/commands");
  String storedPub    = prefs.getString("mqtt_pub",    "puzzles/blow-off-doors/status");

  custom_mqtt_server = new WiFiManagerParameter("server","MQTT Server IP", storedServer.c_str(), 40);
  custom_sub_topic   = new WiFiManagerParameter("sub",   "Subscribe Topic",  storedSub.c_str(),    40);
  custom_pub_topic   = new WiFiManagerParameter("pub",   "Publish Topic",    storedPub.c_str(),    40);

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

  String ip  = custom_mqtt_server->getValue(); ip.toCharArray(mqtt_server, sizeof(mqtt_server));
  String sub = custom_sub_topic->getValue();    sub.toCharArray(mqtt_sub_topic, sizeof(mqtt_sub_topic));
  String pub = custom_pub_topic->getValue();    pub.toCharArray(mqtt_pub_topic, sizeof(mqtt_pub_topic));

  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

  unsigned long start = millis();
  while (!client.connected() && millis() - start < 5000) {
    if (client.connect("esp32-client")) {
      client.subscribe(mqtt_sub_topic);
      mqttConnected = true;
    } else {
      delay(500);
    }
  }
  if (!mqttConnected) {
    wm.resetSettings();
    wm.startConfigPortal(AP_NAME);
    start = millis();
    while (!client.connected() && millis() - start < 5000) {
      if (client.connect("esp32-client")) {
        client.subscribe(mqtt_sub_topic);
        mqttConnected = true;
      } else {
        delay(500);
      }
    }
  }
}

void loop() {
  if (mqttConnected) {
    if (!client.connected()) {
      client.connect("esp32-client");
      client.subscribe(mqtt_sub_topic);
    }
    client.loop();
  }

  bool relayInState = digitalRead(relayIn);
  if (!puzzleSolved && relayInState != lastRelayInState) {
    if (relayInState == LOW) {
      dfPlayer.play(1);
      client.publish(mqtt_pub_topic, "solved");
      onSolve();
    }
    lastRelayInState = relayInState;
  }
}
