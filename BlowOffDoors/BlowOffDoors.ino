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

String clientId = "blow-off-doors-" + String(ESP.getChipRevision()) + "-" + String(random(0xffff), HEX);

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

void setupNetworking() {
  prefs.begin("settings", false);
  String defaultServer = prefs.getString("mqtt_server", "192.168.1.10");
  String defaultSub    = prefs.getString("mqtt_sub",    "puzzles/blow-off-doors/commands");
  String defaultPub    = prefs.getString("mqtt_pub",    "puzzles/blow-off-doors/status");

  custom_mqtt_server = new WiFiManagerParameter("server", "MQTT Server IP", defaultServer.c_str(), 40);
  custom_sub_topic   = new WiFiManagerParameter("sub",    "Subscribe Topic", defaultSub.c_str(),    40);
  custom_pub_topic   = new WiFiManagerParameter("pub",    "Publish Topic",   defaultPub.c_str(),    40);

  WiFiManager wm;
  wm.setSaveConfigCallback(saveConfigCallback);
  wm.addParameter(custom_mqtt_server);
  wm.addParameter(custom_sub_topic);
  wm.addParameter(custom_pub_topic);

  wm.autoConnect(AP_NAME);

  if (shouldSaveConfig) {
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

    newServer.toCharArray(mqtt_server, sizeof(mqtt_server));
    newSub.toCharArray(mqtt_sub_topic, sizeof(mqtt_sub_topic));
    newPub.toCharArray(mqtt_pub_topic, sizeof(mqtt_pub_topic));
  } else {
    defaultServer.toCharArray(mqtt_server, sizeof(mqtt_server));
    defaultSub.toCharArray(mqtt_sub_topic, sizeof(mqtt_sub_topic));
    defaultPub.toCharArray(mqtt_pub_topic, sizeof(mqtt_pub_topic));
  }

  Serial.print("Final MQTT server being used: ");
  Serial.println(mqtt_server);

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
