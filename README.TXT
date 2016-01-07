Arduino-GCode-Sender

This is my Arduino GCode sender Sketch.

I made this because I wanted to be able to use my X-Carve machine with a stand alone controller and be able to submit GCode files from an SD Card.

The arduino sketch works but I am not completely happy with the move function. So that may improve in the future.

HARDWARE:
 1) Arduino Uno 
 2) 4x20 lcd screen with i2c interface 
 3) small analog joystick 
 4) sd card reader 
 5) a few extra buttons will be added later.

CHANGES TO ORIGINAL HARDWARE
The arduino that is delivered with the X-Carve gets its power from the USB cable. With my GRBL controller, there is no USB cable, so you need to power the existing arduino externally (supply 8 volts to the Vin pin)

Use at your own risk.
