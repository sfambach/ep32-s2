
#include "USB.h"
#include "USBHIDMouse.h"
USBHIDMouse Mouse;

#define DELAY_MIN  5000
#define DELAY_MAX 60 * 1000 // 180 * one second = 1.5 minute 
#define DIST_X_MIN 10 // 10 pixel
#define DIST_X_MAX 200 // 10 pixel
#define DIST_Y_MIN 10 // 10 pixel
#define DIST_Y_MAX 180 // 10 pixel


int range = 5;              // output range of X or Y movement; affects movement speed
int responseDelay = 10;     // response delay of the mouse, in ms


void setup() {
  // initialize mouse control:
  Mouse.begin();
  USB.begin();
}

void loop() {
  // read the buttons:

  int distX = random(DIST_X_MIN,DIST_X_MAX);
  int distY = random(DIST_Y_MIN,DIST_Y_MAX);

  // delay between 5 seconds and 3 minutes
  long responseDelay = random(DELAY_MIN, DELAY_MAX);
 
  Mouse.move(-distX, -distY, 0);

  delay(2000);
  // and go back
  Mouse.move(distX, distY, 0);
 
  // a delay so the mouse doesn't move too fast:
  delay(responseDelay);
}
