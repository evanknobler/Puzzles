#include <FastLED.h>

#define NUM_LEDS 3
#define BRIGHTNESS 96
#define FRAMES_PER_SECOND 120

CRGB leds[NUM_LEDS];

void setup() {
  delay(3000);
  FastLED.addLeds<WS2812, 23, GRB>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);

  FastLED.setBrightness(BRIGHTNESS);
}

uint8_t gHue = 180;

void loop() {
  EVERY_N_MILLISECONDS(20) { 
    gHue++; 
    if(gHue>128) { gHue = 0; } 
  } 

  EVERY_N_MILLISECONDS(500) { 
    fill_solid( leds, NUM_LEDS, CHSV(180, 255, 128));

    if( random8() < 128) {
      leds[ random16(NUM_LEDS) ] = CHSV(gHue, 255, 255);
    }
  }

  fadeToBlackBy( leds, NUM_LEDS, 5);

  for(int i=0;i<NUM_LEDS;i++){
    leds[i] |= CHSV(180, 255, 128);
  }

  FastLED.show();
  
  FastLED.delay(1000/FRAMES_PER_SECOND); 
}
