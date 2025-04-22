#include <SoftwareSerial.h>
#include <MIDI.h>

// Create a SoftwareSerial instance for communication with the ESP32.
// We'll use pin 2 for RX (unused here) and pin 3 for TX.
SoftwareSerial espSerial(2, 3);

// Create a default MIDI instance using the hardware Serial (pins 0/1) connected to the MIDI shield.
MIDI_CREATE_DEFAULT_INSTANCE();

// Callback for Note On events.
void handleNoteOn(byte channel, byte pitch, byte velocity) {
  // Format the message as: NOTE_ON,<channel>,<pitch>,<velocity>
  espSerial.print("NOTE_ON,");
  espSerial.print(channel);
  espSerial.print(",");
  espSerial.print(pitch);
  espSerial.print(",");
  espSerial.println(velocity);
}

// Callback for Note Off events.
void handleNoteOff(byte channel, byte pitch, byte velocity) {
  // Format the message as: NOTE_OFF,<channel>,<pitch>,<velocity>
  espSerial.print("NOTE_OFF,");
  espSerial.print(channel);
  espSerial.print(",");
  espSerial.print(pitch);
  espSerial.print(",");
  espSerial.println(velocity);
}

void setup() {
  // Begin the SoftwareSerial port for communication with the ESP32.
  espSerial.begin(115200);
  
  // Initialize the MIDI library on the default hardware Serial.
  MIDI.begin(MIDI_CHANNEL_OMNI);
  
  // Set the MIDI callback functions.
  MIDI.setHandleNoteOn(handleNoteOn);
  MIDI.setHandleNoteOff(handleNoteOff);
  
  // Optionally, initialize hardware Serial for debugging if your shield design permits.
  // Serial.begin(115200);
  // Serial.println("Arduino MIDI buffer ready.");
}

void loop() {
  // Continuously read MIDI messages from the shield.
  MIDI.read();
}
