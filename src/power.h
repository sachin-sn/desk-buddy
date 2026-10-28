#pragma once
// "Soft off": the buddy goes dark and sleeps until the knob is pressed, then restarts cleanly.
// It is not a real power switch (the board stays powered and draws a few mA): for zero draw use a switch in the 5V wire.
//
// Flow: any core calls powerRequestOff(). The UI task (core 1) sees powerOffRequested(), blanks the screen and the
// LEDs and parks itself (powerUiParked()). The network task (core 0) then runs powerDown(): stops WiFi and Bluetooth,
// cuts the display and external 5V rails, waits for the knob to be released, and light-sleeps until it is pressed again.
void powerRequestOff();           // any core, any time
bool powerOffRequested();
bool powerUiParked();
void powerSetUiParked();          // the UI task calls this once the screen and LEDs are off
void powerDown();                 // net task only; returns never (restarts after wake)
