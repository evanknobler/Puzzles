#include <WiFiManager.h>
#include <PubSubClient.h>
#include <DFRobotDFPlayerMini.h>

WiFiClient espClient;
PubSubClient client(espClient);

// Buffers to hold MQTT server IP and topics (persistent)
char mqtt_server[40];
char mqtt_sub_topic[40];
char mqtt_pub_topic[40];

// Custom WiFiManager parameters
WiFiManagerParameter custom_mqtt_server("server", "MQTT Server IP", "192.168.0.100", 40);
WiFiManagerParameter custom_sub_topic("sub", "Subscribe Topic", "puzzles/blow-off-doors/commands", 40);
WiFiManagerParameter custom_pub_topic("pub", "Publish Topic", "puzzles/blow-off-doors/status", 40);

const char* AP_NAME = "Blow Off Doors Setup";

bool mqttConnected = false;

// Initialise a hardware serial interface
HardwareSerial hardwareSerial(2);
// Create an object to access the dfPlayer
DFRobotDFPlayerMini dfPlayer;
// Relay Outputs
const byte relayOut = 22;
const byte relayIn = 23;

bool lastRelayInState = HIGH;
bool puzzleSolved = false;

void callback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (int i = 0; i < length; i++) message += (char)payload[i];
  Serial.print("Received: ");
  Serial.println(message);

  if (message == "solved") {
    onSolve();
    dfPlayer.play(1);
  } else if (message == "reset") {
    resetPuzzle();
  }
}

void onSolve(){
  digitalWrite(relayOut, HIGH);
  puzzleSolved = true;
}

// Make the linear actuator contract
void resetPuzzle(){
  digitalWrite(relayOut, LOW);
  puzzleSolved = false;
}

void setup() {
  Serial.begin(115200);
  hardwareSerial.begin(9600, SERIAL_8N1, 16, 17);
  delay(500);
  if(!dfPlayer.begin(hardwareSerial)) {
    Serial.println("Failed to start DFPlayerMini.");
    while(true);
  }

  // Set volume (value from 0 to 30)
  dfPlayer.volume(30);
  
  // Initialise the relay output
  pinMode(relayIn, INPUT_PULLUP);
  pinMode(relayOut, OUTPUT);
  digitalWrite(relayOut, LOW);

  WiFiManager wm;
  wm.addParameter(&custom_mqtt_server);
  wm.addParameter(&custom_sub_topic);
  wm.addParameter(&custom_pub_topic);

  // Try auto connect; open config portal if WiFi fails
  bool wifiSuccess = wm.autoConnect(AP_NAME);
  if (!wifiSuccess) {
    Serial.println("WiFi failed or timed out.");
    // It will remain in AP mode
  }

  // Copy input fields into char[] buffers
  String ipStr = custom_mqtt_server.getValue(); ipStr.trim(); ipStr.toCharArray(mqtt_server, sizeof(mqtt_server));
  String subStr = custom_sub_topic.getValue(); subStr.trim(); subStr.toCharArray(mqtt_sub_topic, sizeof(mqtt_sub_topic));
  String pubStr = custom_pub_topic.getValue(); pubStr.trim(); pubStr.toCharArray(mqtt_pub_topic, sizeof(mqtt_pub_topic));

  Serial.println("MQTT IP: " + String(mqtt_server));
  Serial.println("Sub topic: " + String(mqtt_sub_topic));
  Serial.println("Pub topic: " + String(mqtt_pub_topic));

  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

  // Try connecting to MQTT broker
  unsigned long startAttempt = millis();
  while (!client.connected() && millis() - startAttempt < 5000) {
    Serial.print("Trying MQTT...");
    if (client.connect("esp32-client")) {
      Serial.println("connected!");
      client.subscribe(mqtt_sub_topic);
      mqttConnected = true;
    } else {
      Serial.print(".");
      delay(500);
    }
  }

  if (!mqttConnected) {
    Serial.println("MQTT connection failed. Reopening WiFiManager config portal.");
    wm.resetSettings();  // Clear saved credentials
    wm.startConfigPortal(AP_NAME);

    // Extract values again after config portal is closed
    String ipStr = custom_mqtt_server.getValue(); ipStr.trim(); ipStr.toCharArray(mqtt_server, sizeof(mqtt_server));
    String subStr = custom_sub_topic.getValue(); subStr.trim(); subStr.toCharArray(mqtt_sub_topic, sizeof(mqtt_sub_topic));
    String pubStr = custom_pub_topic.getValue(); pubStr.trim(); pubStr.toCharArray(mqtt_pub_topic, sizeof(mqtt_pub_topic));

    Serial.println("Updated MQTT IP: " + String(mqtt_server));
    Serial.println("Updated Sub topic: " + String(mqtt_sub_topic));
    Serial.println("Updated Pub topic: " + String(mqtt_pub_topic));

    client.setServer(mqtt_server, 1883);
    client.setCallback(callback);

    // Try connecting to MQTT again
    unsigned long retryStart = millis();
    while (!client.connected() && millis() - retryStart < 5000) {
      Serial.print("Retrying MQTT...");
      if (client.connect("esp32-client")) {
        Serial.println("connected!");
        client.subscribe(mqtt_sub_topic);
        mqttConnected = true;
      } else {
        Serial.print(".");
        delay(500);
      }
    }

    if (!mqttConnected) {
      Serial.println("Still failed to connect to MQTT.");
    }
  }
}

void loop() {
  if (mqttConnected) {
    if (!client.connected()) {
      Serial.println("MQTT lost. Reconnecting...");
      if (client.connect("esp32-client")) {
        client.subscribe(mqtt_sub_topic);
      }
    }
    client.loop();
  }
  
  bool relayInState = digitalRead(relayIn);
  // Serial.print("Relay In State: ");
  // Serial.println(relayInState);

  if (!puzzleSolved) {
    if (relayInState != lastRelayInState) {
      if(relayInState == LOW) {
        dfPlayer.play(1);
        const char* message = "solved";
        client.publish(mqtt_pub_topic, message);
      }
      lastRelayInState = relayInState;
    }
  }
}