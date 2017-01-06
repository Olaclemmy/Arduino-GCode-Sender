/*
GCode Sender.
Hardware: Arduino Uno,
         ,analog thumbstick (small joystick, with intgrated push button)
         ,sd card reader with SPI interface
         ,a general 4x20 LCD display with i2c interface

Limitations: It does not support directories on the SD card, only files in the root directory are supported.
      
(Micro) SD card attached to SPI bus as follows:
CS - pin 10
MOSI - pin 11
MISO - pin 12
CLK - pin 13

Joystick attached af follows:
x value - pin A0
y value - pin A1
push button - pin 9
NOTE: My joystick is mounted sideways so x = vertical, y = horizontal

LCD Display
the LCD display is connected to pins A4 and A5 (default i2c connections)

Arduino pins:
0 rx : connects to tx on other arduino
1 tx : connects to rx on other arduino
D2   : Button 12 o clock
D3   : Button  3 o clock
D4   : Button  6 o clock
D5   : Button  9 o clock
D6   : Connected to the reset pin to be able to self reset
D7   :
D8   :
D9   : Joystick switch
D10  : SD Card CS
D11  : SD Card MOSI
D12  : SD Card MISO
D13  : SD Card Clock
A0   : Joystick x value
A1   : Joystick y value
A2   :
A3   :
A4   : LCD Display (SDA)
A5   : LCD Display (SCL)
*/

#include <Wire.h> 
#include <SPI.h>
#include <SD.h>

// downloaded from https://bitbucket.org/fmalpartida/new-liquidcrystal/downloads
// I used NewliquidCrystal_1.3.4.zip
#include <LiquidCrystal_I2C.h> 

#define SD_card_Reader  10 
#define joystick_xPin   A0  
#define joystick_yPin   A1  
#define joystick_switch  9   

#define Msw12 2
#define Msw3  3
#define Msw6  4
#define Msw9  5

#define resetpin 6

// Display
LiquidCrystal_I2C lcd(0x27, 2, 1, 0, 4, 5, 6, 7, 3, POSITIVE);  // Set the LCD I2C address


// Globals
// There are a few global variables
char WposX[9];            // last known X pos on workpiece, space for 9 characters ( -999.999\0 )
char WposY[9];            // last known Y pos on workpiece
char WposZ[9];            // last known Z heighton workpiece, space for 8 characters is enough( -99.999\0 )
char MposX[9];            // last known X pos absolute to the machine
char MposY[9];            // last known Y pos absolute to the machine
char MposZ[9];            // last known Z height absolute to the machine

char machineStatus[10]; // last know state (Idle, Run, Hold, Door, Home, Alarm, Check)

bool awaitingOK = false;   // this is set true when we are waiting for the ok signal from the grbl board (see the sendCodeLine() void)

void setup() {
  // inputs (write high to enable pullup)
  pinMode(joystick_switch,INPUT); digitalWrite(joystick_switch,HIGH);
  pinMode(Msw12,INPUT);digitalWrite(Msw12,HIGH); 
  pinMode(Msw3,INPUT); digitalWrite(Msw3,HIGH);
  pinMode(Msw6,INPUT); digitalWrite(Msw6,HIGH);
  pinMode(Msw9,INPUT); digitalWrite(Msw9,HIGH);
  digitalWrite(resetpin,HIGH);
  pinMode(resetpin,OUTPUT);
  // display 
  lcd.begin(20, 4);

  // init the sd card reader
  if (!SD.begin(SD_card_Reader)) {    
     setTextDisplay(F("Error"),F("SD Card Fail!"),"",F("Auto RESET in 3 sec"));
     delay(3000);
     lcd.clear(); delay(500);
     digitalWrite(resetpin,LOW);
  }   
 
  // Ask to connect (you might still use a computer and NOT connect this controller)
  setTextDisplay("",F("      Connect?    "),"","");
  while (digitalRead(joystick_switch)) {}  // wait for the button to be pressed
  delay(50);
  // serial connection
  Serial.begin(115200);
  while (!digitalRead(joystick_switch)) {}  // be sure the button is released before we continue
  delay(50);
     
}


void emergencyBreak(){
  Serial.print(F("!"));  // feed hold
  setTextDisplay(F("Pause"),F("Green = Continue"),F("Red = Reset"),"");
  while (true) {
    if (!digitalRead(Msw3)) {Serial.print("~");return;} // send continue and return
    if (!digitalRead(Msw6)) {
       softReset();      // reset the grbl shield
       resetSDReader() ; // reset the sd card reader
       sendCodeLine(F("G0 Z10")); // lift the tool
       delay(1000);
       sendCodeLine(F("G0 X0 Y0")); //return to zero
       delay(1000);
       sendCodeLine(F("G0 Z0")); //return to zero
       digitalWrite(resetpin,LOW);
    }
  }
}


byte fileMenu() {
  /*
  This is the file menu.
  You can browse up and down to select a file.
  Click the button to select a file

  Move the stick right to exit the file menu and enter the Move menu
  */
  byte fileindex=1;
  String fn; 
  byte fc = filecount();
  int xval,yval;

  fn= getFileName(fileindex);
  setTextDisplay(F("Files ")," -> " + (String)fn,"",F("Click to select"));
        
  while (true){
    xval=analogRead(joystick_xPin);
    yval=analogRead(joystick_yPin);
   
    if (fileindex < fc && xval < 30) { // joystick down!            
      fileindex++;
      fn= getFileName(fileindex);
      lcd.setCursor(0, 1);
      lcd.print(F(" -> ")); lcd.print(fn);
      waitForJoystickMid();
      
      }
      
    if (xval > 900) { // joystick up!
      if (fileindex > 1) {      
        fileindex--;
        fn= getFileName(fileindex);
        lcd.setCursor(0, 1);
        lcd.print(F(" -> ")); lcd.print(fn);
        waitForJoystickMid();
      }
    }

  
    if (fileindex > 0 && digitalRead(joystick_switch)==LOW && fn!="") {    // Pushed it!    
       
       setTextDisplay(F("Send this file? ")," -> " + fn,"",F("Click to confirm"));  // Ask for confirmation
       delay(50);
       while (digitalRead(joystick_switch)==LOW) {} // Wait for the button to be released
       delay(50);

       unsigned long t = millis();
       while (millis()-t <= 1500UL) {
         if (digitalRead(joystick_switch)==LOW) {  // Press the button again to confirm
           delay(10);
           while (digitalRead(joystick_switch)==LOW) {} // Wait for the button to be released
           return fileindex;  
           break;
           }
         }
         setTextDisplay(F("Files ")," -> " + fn,"",F("Click to select"));
     }
     
    // joystick to right exits this menu and joystick_switches to MOVE menu    
    if (yval > 900) { // full right!
       waitForJoystickMid();
       return 0;    
       setTextDisplay(F("Files ")," -> " + fn,"",F("Click to select"));             
       }
   }   
}

void waitForJoystickMid() {
   int xval,yval;
   while(true) { // wait for the joystick to be release (back to mid position)
          xval=analogRead(joystick_xPin);
          yval=analogRead(joystick_yPin);
          if ((xval >= 500 && xval <= 600) && (yval >= 500 && yval <= 600)) {break;}    
        }
  }
  
void moveMenu(){
  /*
  This is the Move menu,
  X and Y move with the joystick
  Z moves with the buttons
    switch at 12 o clock = up in big steps
    switch at  9 o clock = down in big steps
    switch at  3 o clock = up in small steps
    switch at  6 o clock = down in small steps
    
  Exit the menu by clicking the joystick
  */
  String MoveCommand,SpeedCommand;
  bool hardup,harddown,slowup, slowdown, updateDisplay;
  int xval,yval,qtime;
  unsigned long queue=0; // queue length in milliseconds
  unsigned long startTime,lastUpdate;
  
  char sln1[21];
  char sln2[21];
  char sln3[21];
  char sln4[21];
  char sla[30];
  char slb[30];
  // Set zero point
  sendCodeLine(F("G21"));
  sendCodeLine(F("G92 X0 Y0 Z0")); 
   
  while (MoveCommand!="-1") { 
    // read the state of all inputs
    xval=analogRead(joystick_xPin);
    yval=analogRead(joystick_yPin);
    hardup   = !digitalRead(Msw12);
    harddown = !digitalRead(Msw9);
    slowup   = !digitalRead(Msw3);
    slowdown = !digitalRead(Msw6);

    if (MoveCommand=="") startTime = millis();
    // get the status of the machine and monitor the receive buffer for OK signals
    if (millis() - lastUpdate >= 500) {
      getStatus();
      lastUpdate=millis(); 
      updateDisplay = true;   
    }
    qtime=300;
    bool joyCenter=true;
    if (yval < 30)  {
      SpeedCommand=F(" F600"); MoveCommand=F("G1 X-3"); joyCenter=false;  // Full left
      } else {
         if (yval < 300) {
          SpeedCommand=F(" F200"); MoveCommand=F("G1 X-1"); joyCenter=false; // Left
         } else {
          if (yval > 900) {
             SpeedCommand=F(" F600"); MoveCommand=F("G1 X3"); joyCenter=false;  // Full right
             } else {
               if (yval > 600) {
                SpeedCommand=F(" F200"); MoveCommand=F("G1 X1"); joyCenter=false; // Right
               }
             }
         }
      }
    
    
    if (xval < 30)  {
      SpeedCommand=F(" F600"); MoveCommand=F("G1 Y-3");joyCenter=false; // Full down
      } else {
        if (xval < 300) {SpeedCommand=F(" F200"); MoveCommand=F("G1 Y-1");joyCenter=false;   // Down
        } else {
          if (xval > 900) {
            SpeedCommand=F(" F600"); MoveCommand=F("G1 Y3");joyCenter=false;  // Full up
            }  else {
               if (xval > 600) {
                  SpeedCommand=F(" F200"); MoveCommand=F("G1 Y1");joyCenter=false;  // Up           
                }
              }
          }  
        }
       
    
    bool noButtons = true;
    if (slowup)  {qtime=5;SpeedCommand = F("F150"); MoveCommand=F("G1 Z0.1 "); noButtons =false;}  // Up Z
    if (hardup)  {qtime=30;SpeedCommand = ""; MoveCommand=F("G0 Z2");noButtons =false;}            // Full up Z
    if (slowdown){qtime=5;SpeedCommand = F("F150"); MoveCommand=F("G1 Z-0.1 ");noButtons =false;}  // Down Z
    if (harddown){qtime=30;SpeedCommand = ""; MoveCommand=F("G0 Z-2");noButtons =false;}           // Full down Z

    if (MoveCommand!="" && (millis()+300) > (startTime + queue)) {
      // send the commands
      sendCodeLine(SpeedCommand); 
      sendCodeLine(MoveCommand);        
      sendCodeLine(F("G92 X0 Y0 Z0"));
      queue += qtime;
    }
   
    
    if (joyCenter && noButtons && MoveCommand!="" ) {
      softReset();
      sendCodeLine(F("G21"));
      sendCodeLine(F("G92 X0 Y0 Z0"));         
      MoveCommand="";
      queue=0;
      getStatus();
      updateDisplay = true;
    }      
      
    if (updateDisplay) {
      updateDisplay = false;
      strcpy(sln1,"X: ");
      strcat(sln1,MposX);
      strcat(sln1,'\0');
      
      strcpy(sln2,"Y: ");
      strcat(sln2,MposY);
      strcat(sln2,'\0');
      
      strcpy(sln3,"Z: ");
      strcat(sln3,MposZ);
      strcat(sln4,'\0');
      
      strcpy(sln4,"Click stick to exit");
      strcat(sln4,'\0');
      
      strcpy(slb,MposX);
      strcat(slb,MposY);
      strcat(slb,MposZ);
      strcat(slb,'\0');
      
      if (sla != slb) {
        setTextDisplay(sln1,sln2,sln3,sln4);        
        strcpy(sla,slb);
      }
    }
    
    if (digitalRead(joystick_switch)==LOW) { // button is pushed, exit the move loop      
      softReset();
      MoveCommand=F("-1");      
      while (digitalRead(joystick_switch)==LOW) {};
      delay(10);
    } 
  }
  

}
  
String getFileName(byte i){
  /*
    Returns a filename.
    if i = 1 it returns the first file
    if i = 2 it returns the second file name
    if i = 3 ... get it?
  */
  byte x = 0;
  String result;
  File root = SD.open("/");    
  while (result=="") {
    File entry =  root.openNextFile();
    if (!entry) {    
        // noting         
        } else {
          if (!entry.isDirectory()) {
            x++;
            if (x==i) result=entry.name();                         
          }
          entry.close(); 
        }
  }
  root.close();  
  return result;
}

byte filecount(){
  /*
    Count the number of files on the SD card.
  */
  
  byte c =0;
   File root = SD.open("/"); 
   while (true) {
    File entry =  root.openNextFile();
    if (! entry) {
      root.rewindDirectory(); 
      root.close(); 
      return c; 
      break;} else  {
         if (!entry.isDirectory()) c++;    
         entry.close();
        }  
   }
}
  

void setTextDisplay(String line1, String line2, String line3, String line4){
  /*
   This writes text to the display
  */
    lcd.clear(); 
    lcd.print(line1.substring(0,19));
    lcd.setCursor(0, 1);
    lcd.print(line2.substring(0,19));
    lcd.setCursor(0, 2);
    lcd.print(line3.substring(0,19));
    lcd.setCursor(0, 3);
    lcd.print(line4.substring(0,19));       
}


void sendFile(byte fileIndex){   
  /*
  This procedure sends the cgode to the grbl shield, line for line, waiting for the ok signal after each line

  It also queries the machine status every 200 milliseconds and writes some status information on the display
  */
  String strLine="";
 
  File dataFile;
  unsigned long lastUpdate;
  unsigned long runningTime;
 
  String filename;
  filename= getFileName(fileIndex);
  dataFile = SD.open(filename);
  if (!dataFile) {
    setTextDisplay(F("File"),"", F("Error, file not found"),"");
    delay(1000); // show the error
    return;
    }

   // Set the Work Position to zero
  sendCodeLine(F("G21"));
  sendCodeLine(F("G92 X0 Y0 Z0"));  // set zero
  clearRXBuffer();
  // reset the timer
  runningTime = millis();
  
  // Read the file and send it to the machine
  while ( dataFile.available() ) {
    
    if (!awaitingOK) { 
      // If we are not waiting for OK, send the next line      
      strLine = dataFile.readStringUntil('\n'); 
      strLine = ignoreUnsupportedCommands(strLine);
      if (strLine !="") sendCodeLine(strLine);    // sending it!  
    }

    // get the status of the machine and monitor the receive buffer for OK signals
    if (millis() - lastUpdate >= 250) {      
      lastUpdate=millis();  
      updateDisplayStatus(runningTime);          
    }
    if (!digitalRead(Msw12)) {emergencyBreak();}
  }
  
  
  /* 
   End of File!
   All Gcode lines have been send but the machine may still be processing them
   So we query the status until it goes Idle
  */
  
   while (strcmp (machineStatus,"Idle") != 0) {
    if (!digitalRead(Msw12)) {emergencyBreak();}
     delay(250);
     getStatus();
     updateDisplayStatus(runningTime);          
   }
   // Now it is done.
   
   sendCodeLine(F("G0 Z 0 "));  // return Z to zero    
   delay(1200);     
   
   lcd.setCursor(0, 1);
   lcd.print(F("                ")); 
   lcd.setCursor(0, 2);
   lcd.print(F("                ")); 
   lcd.setCursor(0, 3);
   lcd.print(F("                "));
   while (digitalRead(joystick_switch)==HIGH) {} // Wait for the button to be pressed
   delay(50);
   while (digitalRead(joystick_switch)==LOW) {} // Wait for the button to be released
   delay(50);
   
   dataFile.close();

   resetSDReader();
   
   return; 
}

void updateDisplayStatus(unsigned long runtime){
  /*
   I had some issues with updating the display while carving a file
   I created this extra void, just to update the display while carving.
  */

  unsigned long t = millis() - runtime;
  int H,M,S;
  char timeString[9];
  char p[3];
  
  t=t/1000;
  // Now t is the a number of seconds.. we must convert that to "hh:mm:ss"
  H = floor(t/3600);
  t = t - (H * 3600);
  M = floor(t/60);
  S = t - (M * 60);

  sprintf(p,"%02d",H);
  strcpy(timeString,p);
  strcat(timeString,":");

  sprintf(p,"%02d",M);
  strcat(timeString,p);
  strcat(timeString,":");
  
  sprintf(p,"%02d",S);
  strcat(timeString,p);
  
  timeString[8]= '\0';
  
  getStatus();
  lcd.clear();
  lcd.print(machineStatus);
  lcd.print(" ");
  lcd.print(timeString);
  lcd.setCursor(0, 1);
  lcd.print(F("X: "));  lcd.print(WposX);
  lcd.setCursor(0, 2);
  lcd.print(F("Y: "));  lcd.print(WposY);
  lcd.setCursor(0, 3);
  lcd.print(F("Z: "));  lcd.print(WposZ);
  }

void resetSDReader() {
  /* 
   This next SD.begin is to fix a problem, I do not like it but there you go.
   Without this sd.begin, I could not open anymore files after the first run.

   To make this work I have changed the SD library a bit (just added one line of code)
   I added root.close() to SD.cpp
   as explained here http://forum.arduino.cc/index.php?topic=66415.0
  */
  
   while (!SD.begin(SD_card_Reader)) {    
     setTextDisplay(F("Error"),F("SD Card Fail!"),"","");
     delay(2000); 
     }
}


void sendCodeLine(String lineOfCode ){
  /*
    This void sends a line of code to the grbl shield, the grbl shield will respond with 'ok'
    but the response may take a while.
    So we immediately check for a response, if we get it, great!
    if not, we set the awaitingOK variable to true, this tells the sendfile() to stop sending code
    We continue to monitor the rx buffer for the 'ok' signal in the getStatus() procedure.
  */
  Serial.println(lineOfCode);
  awaitingOK = true;  
  delay(10);
  checkForOk();
  
}
  
void clearRXBuffer(){
  /*
  Just a small void to clear the RX buffer.
  */
  char v;
    while (Serial.available()) {
      v=Serial.read();
      delay(3);
    }
  }
  
String ignoreUnsupportedCommands(String lineOfCode){
  /*
  Remove unsupported codes, either because they are unsupported by GRBL or because I choose to.  
  */
  removeIfExists(lineOfCode,F("G64"));   // Unsupported: G64 Constant velocity mode 
  removeIfExists(lineOfCode,F("G40"));   // unsupported: G40 Tool radius comp off 
  removeIfExists(lineOfCode,F("G41"));   // unsupported: G41 Tool radius compensation left
  removeIfExists(lineOfCode,F("G81"));   // unsupported: G81 Canned drilling cycle 
  removeIfExists(lineOfCode,F("G83"));   // unsupported: G83 Deep hole drilling canned cycle 
  removeIfExists(lineOfCode,F("M6"));    // ignore Tool change
  removeIfExists(lineOfCode,F("M7"));    // ignore coolant control
  removeIfExists(lineOfCode,F("M8"));    // ignore coolant control
  removeIfExists(lineOfCode,F("M9"));    // ignore coolant control
  removeIfExists(lineOfCode,F("M10"));   // ignore vacuum, pallet clamp
  removeIfExists(lineOfCode,F("M11"));   // ignore vacuum, pallet clamp
  removeIfExists(lineOfCode,F("M5"));    // ignore spindle off
  lineOfCode.replace(F("M2 "),"M5 M2 "); // Shut down spindle on program end.
  
  // Ignore comment lines 
  // Ignore tool commands, I do not support tool changers
  if (lineOfCode.startsWith("(") || lineOfCode.startsWith("T") ) {lineOfCode="";}    

  lineOfCode.trim();
  
  return lineOfCode;
}

String removeIfExists(String lineOfCode,String toBeRemoved ){
  if (lineOfCode.indexOf(toBeRemoved) >= 0 ) lineOfCode.replace(toBeRemoved," ");
  return lineOfCode;
}

  
void checkForOk() {
  // read the receive buffer (if anything to read)
  char c,lastc;
   while (Serial.available()) {
    c = Serial.read();    
    if (lastc=='o' && c=='k') {awaitingOK=false;}
    lastc=c;
    delay(1);     
    }
}

void softReset(){
  /*
  This stops the machine immediately (feed hold signal)
  And then sends the soft reset signal to clear the command buffer
  */
  Serial.print(F("!"));  // feed hold
  delay(300);            
  Serial.write(24); // soft reset, clear command buffer          
  delay(200);            
  // clear the RX receive buffer
  clearRXBuffer();
}


void getStatus(){
  /*
    This gets the status of the machine
    The status message of the machine might look something like this (this is a worst scenario message)
    The max length of the message is 72 characters long (including carriage return).
    
    <Check,MPos:-995.529,-210.560,-727.000,WPos:-101.529,-115.440,-110.000>
    
  */
  
  char content[80];
  char character;
  byte index=0;
  bool completeMessage=false;
  int i=0;
  int c=0;
  checkForOk();

  Serial.print(F("?"));  // Ask the machine status
  while (Serial.available() == 0) { }  // Wait for response 
  while (Serial.available()) {
    content[index] = Serial.read();  
    if (content[index] =='>') completeMessage=true; // a simple check to see if the message is complete
    if (index>0) {if (content[index]=='k' && content[index-1]=='o') {awaitingOK=false;}}
    index++;
    delay(1); 
    }
  
  if (!completeMessage) { return; }   
  
  i++;
  while (c<9 && content[i] !=',') {machineStatus[c++]=content[i++]; machineStatus[c]=0; } // get the machine status
  while (content[i++] != ':') ; // skip until the first ':'
  c=0;
  while (c<8 && content[i] !=',') { MposX[c++]=content[i++]; MposX[c] = 0;} // get MposX
  c=0; i++;
  while (c<8 && content[i] !=',') { MposY[c++]=content[i++]; MposY[c] = 0;} // get MposY
  c=0; i++;
  while (c<8 && content[i] !=',') { MposZ[c++]=content[i++]; MposZ[c] = 0;} // get MposZ
  while (content[i++] != ':') ; // skip until the next ':'
  c=0;
  while (c<8 && content[i] !=',') { WposX[c++]=content[i++]; WposX[c] = 0;} // get WposX
  c=0; i++;
  while (c<8 && content[i] !=',') { WposY[c++]=content[i++]; WposY[c] = 0;} // get WposY
  c=0; i++;
  while (c<8 && content[i] !='>') { WposZ[c++]=content[i++]; WposZ[c] = 0;} // get WposZ
}

void loop() {  
  byte a;
  a = fileMenu(); 
  if (a==0) {
    moveMenu();
  } else {
    sendFile(a);
  }
}


