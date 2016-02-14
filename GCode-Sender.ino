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

// Display
LiquidCrystal_I2C lcd(0x27, 2, 1, 0, 4, 5, 6, 7, 3, POSITIVE);  // Set the LCD I2C address


// Globals
// There are a few global varialbles
char WposX[9];            // last known X pos on workpiece, space for 9 characters ( -999.999\0 )
char WposY[9];            // last known Y pos on workpiece
char WposZ[9];            // last known Z heighton workpiece, space for 8 characters is enough( -99.999\0 )
char MposX[9];            // last known X pos absolute to the machine
char MposY[9];            // last known Y pos absolute to the machine
char MposZ[9];            // last known Z height absolute to the machine

char machineStatus[10]; // last know state (Idle, Run, Hold, Door, Home, Alarm, Check)
unsigned long runningTime; // this will hold the timevalue when a carve job starts, so it can calculate the duration
bool awaitingOK = false;   // this is set true when we are waiting for the ok signal from the grbl board (see the sendCodeLine() void)


void setup() {
  // inputs and outputs
  pinMode(joystick_switch,INPUT_PULLUP); 

  // display 
  lcd.begin(20, 4);

  // init the sd card reader
  if (!SD.begin(SD_card_Reader)) {    
     setTextDisplay("", F("Error"),F("SD Card Fail!"),"");
     delay(1000);
     return;
  }   
 
  // Ask to connect (you might still use a computer and NOT connect this controller)
  setTextDisplay("",F("      Connect?    "),"","");
  while (digitalRead(joystick_switch)) {}  // wait for the button to be pressed
  delay(50);
  // serial connection
  Serial.begin(115200);
  while (!digitalRead(joystick_switch)) {}  // be sure the button is released before we continue
  delay(50);
  fileMenu();    
}

void fileMenu() {
  /*
  This is the file menu.
  You can browse up and down to select a file.
  Click the button to select a file

  Move the stick right to exit the file menu and enter the Move menu
  */
  
  static byte fileindex=1;
  static String fn; 
  byte fc = filecount();
  int xval,yval;

  fn= getFileName(fileindex);
  setTextDisplay(F("Files ")," -> " + fn,"",F("Click to select"));
        
  while (true){
    xval=analogRead(joystick_xPin);
    yval=analogRead(joystick_yPin);
   
    if (fileindex < fc && xval < 30) { // joystick down!            
      fileindex++;
      fn= getFileName(fileindex);
      setTextDisplay(F("Files ")," -> " + fn,"",F("Click to select"));
      waitForJoystickMid();
      
      }
      
    if (xval > 900) { // joystick up!
      if (fileindex > 1) {      
        fileindex--;
        fn= getFileName(fileindex);
        setTextDisplay(F("Files ")," -> " + fn,"",F("Click to select"));
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
           sendFile(fn);   
           break;
           }
         }
         setTextDisplay(F("Files ")," -> " + fn,"",F("Click to select"));
     }
     
    // joystick to right exits this menu and joystick_switches to MOVE menu    
    if (yval > 900) { // full right!
       waitForJoystickMid();
       moveMenu();    
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
  the menu has 2 options, XY move or Z move
  Exit the menu by moving the stick left (back to the file menu
  */
  int opt0=2;
  int option=1;
  int xval,yval;  
    
  while (true){
    xval=analogRead(joystick_xPin);
    yval=analogRead(joystick_yPin);
  
    if (xval < 30)  option=2;    // joystick down!      
    if (xval > 900) option=1;    // joystick up!    
        
    if (option==1 && opt0!=1){opt0=1; setTextDisplay(F("Move  > xy move"),F("         z move"),"","");}
    if (option==2 && opt0!=2){opt0=2; setTextDisplay(F("Move    xy move"),F("      >  z move"),"","");} 

    if ((xval >= 500 && xval <= 600) && (yval >= 500 && yval <= 600) && digitalRead(joystick_switch)==LOW) { // pushed the button!
    while (digitalRead(joystick_switch)==LOW) {};             // wait for the button to be released
    delay(10);        
    xyzMove(option); 
    opt0=0;       
  }
  
    // joystick to left exits this menu and joystick_switchitches back to File menu           
    if (yval < 30) {break;} 
  }      
  return;   
}
  
void xyzMove(int moveAxis)  {
  /*
  This procedure enables us to move the X-Carve with the joystick.

  Disclaimer: This is cowboy code, it works but might be full of good examples of bad practice

  This is how it works:
  First I reset the position to zero with G92 X0 Y0 Z0
  Then the joystick sends the machine to a new location, for example F600 G1 X3  
  Then I reset the position again to zero G92 X0 Y0 Z0.
  So there is no reliable information on the position of the machine.

  If the joystick is returned to the mid position while the machine is still moving, I send the feed hold command (!) and then
  clear the command buffer by soft sending the reset signal, then I reset the position again with G92 X0 Y0 Z0.
  
  */
  
 
  String MoveCommand,SpeedCommand;
  int xval,yval;
  unsigned long queue=0; // queue length in milliseconds
  unsigned long startTime,lastUpdate;
  String sl1,sl2,sl3,sl4,sl0;
  // Set zero point
  sendCodeLine(F("G21"));
  sendCodeLine(F("G92 X0 Y0 Z0"));    
  
  while (MoveCommand!="-1") {   
    xval=analogRead(joystick_xPin);
    yval=analogRead(joystick_yPin);
       
    if (MoveCommand=="") startTime = millis();

    // get the status of the machine and monitor the receive buffer for OK signals
    if (millis() - lastUpdate >= 200) {
      getStatus();
      lastUpdate=millis();      
    }

    if (moveAxis==1) {
       
      if (yval < 300) {SpeedCommand=F(" F200"); MoveCommand=F("G1 X-1");} // Left
      if (yval < 30)  {SpeedCommand=F(" F600"); MoveCommand=F("G1 X-3");} // Full left
    
      if (yval > 600) {SpeedCommand=F(" F200"); MoveCommand=F("G1 X1");}  // Right
      if (yval > 900) {SpeedCommand=F(" F600"); MoveCommand=F("G1 X3");}  // Full right
    
      if (xval > 600) {SpeedCommand=F(" F200"); MoveCommand=F("G1 Y1");}  // Up 
      if (xval > 900) {SpeedCommand=F(" F600"); MoveCommand=F("G1 Y3");}  // Full up
    
      if (xval < 300) {SpeedCommand=F(" F200"); MoveCommand=F("G1 Y-1");} // Down
      if (xval < 30)  {SpeedCommand=F(" F600"); MoveCommand=F("G1 Y-3");} // Full down

      sl1="    ->   X: " + (String)MposX;
      sl2="    ->   Y: " + (String)MposY;
      sl3="         Z: " + (String)MposZ;
      sl4="Move X/Y to W-zero" ;
      if (sl0 != (String)MposX + (String)MposY){
         setTextDisplay(sl1,sl2,sl3,sl4);        
         sl0=  (String)MposX + (String)MposY;
        }
      
      }
      
     if (moveAxis==2) {
      
      if (xval > 600) {SpeedCommand = F("F150"); MoveCommand=F("G1 Z 0.05 ");}   // Up 
      if (xval > 900) {SpeedCommand = ""; MoveCommand=F("G0 Z 0.5");}            // Full up
      if (xval < 300) {SpeedCommand = F("F150"); MoveCommand=F("G1 Z -0.05 ");}  // Down 
      if (xval < 30)  {SpeedCommand = ""; MoveCommand=F("G0 Z -0.5");}           // Full down
      
      sl1="         X: " + (String)MposX;
      sl2="         Y: " + (String)MposY;
      sl3="    ->   Z: " + (String)MposZ;
      sl4="Move Z to W-zero";
      if ( sl3 != sl0 ){
         setTextDisplay(sl1,sl2,sl3,sl4);  
         sl0=sl3;
        }
      
    
    }
     
    if (MoveCommand!="" && (millis()+300) > (startTime + queue)) {
      // send the commands
      sendCodeLine(SpeedCommand); 
      sendCodeLine(MoveCommand);        
      sendCodeLine(F("G92 X0 Y0 Z0"));
      if (SpeedCommand ==""){ queue += 80;} else {queue += 300;}
    }
    
    if ((xval >= 500 && xval <= 600) && (yval >= 500 && yval <= 600)  && MoveCommand!="") {
      softReset();
      sendCodeLine(F("G21"));
      sendCodeLine(F("G92 X0 Y0 Z0"));         
      MoveCommand="";
      queue=0;
      getStatus();
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
  String result="";
  File root = SD.open("/");    
  while (result=="") {
    File entry =  root.openNextFile();
    if (!entry) {    
        result="--" + String(x);         
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


void sendFile(String filename){   
  /*
  This procedure sends the cgode to the grbl shield, line for line, waiting for the ok signal after each line

  It also queries the machine status every 200 milliseconds and writes some status information on the display
  */
  String strLine="";
  String finalTime;
  File dataFile;
  unsigned long lastUpdate;
 
  
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
      updateDisplayStatus();          
    }
    
  }
  
  
  /* 
   End of File!
   All Gcode lines have been send but the machine may still be processing them
   So we query the status until it goes Idle
  */
  
   while (strcmp (machineStatus,"Idle") != 0) {
     delay(250);
     getStatus();
     updateDisplayStatus();          
   }
   // Now it is done.
   
   sendCodeLine(F("G0 Z 0 "));  // return Z to zero    
   delay(1200);     
   
   setTextDisplay("Idle " + getTime(),""," click  to finish ","");     
   while (digitalRead(joystick_switch)==HIGH) {} // Wait for the button to be pressed
   delay(50);
   while (digitalRead(joystick_switch)==LOW) {} // Wait for the button to be released
   delay(50);
   
   dataFile.close();

   resetSDReader();
   
   return; 
}

void updateDisplayStatus(){
  /*
   I had some issues with updating the display while carving a file
   I created this extra void, just to update the display while carving.
  */
  getStatus();
  lcd.clear();
  lcd.print(machineStatus);
  lcd.print(" ");
  lcd.print(getTime());
  lcd.setCursor(0, 1);
  lcd.print("         X: ");  lcd.print(WposX);
  lcd.setCursor(0, 2);
  lcd.print("         Y: ");  lcd.print(WposY);
  lcd.setCursor(0, 3);
  lcd.print("         Z: ");  lcd.print(WposZ);
  }

void resetSDReader() {
  /* 
   This next SD.begin is to fix a problem, I do not like it but there you go.
   Without this sd.begin, I could not open anymore files after the first run.

   To make this work I have changed the SD library a bit (just added one line of code)
   I added root.close() to SD.cpp
   as explained here http://forum.arduino.cc/index.php?topic=66415.0
  */
  
   if (!SD.begin(SD_card_Reader)) {    
     setTextDisplay(F("Error"),F("SD Card Fail!"),"","");
     delay(1000); 
     }
  }

  
String getTime(){
  /*
    When a job starts, we set the runningTime variable using the millis counter.
    This procedure calculates how many milliseconds the job is running and gives the time back as a string
  */
  unsigned long t = millis() - runningTime;
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
  return timeString;  
}

void sendCodeLine(String lineOfCode ){
  /*
    This void sends a line of code to the grbl shield, the grbl shield will respond with 'ok'
    but the response may take a while.
    So we immediately check for a response, if we get it, great!
    if not, we set the awaitingOK variable to true, this tells the sendfile() to stop sending code
    We continue to monitor the rx buffer for the 'ok' signal in the getStatus() procedure.
  */
  
  // clearRXBuffer();
  Serial.println(lineOfCode);
  // wait for one second for the ok response
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
  
  lineOfCode.replace(F("G64")," "); // Unsupported: G64 Constant velocity mode 
  lineOfCode.replace(F("G40")," "); // unsupported: G40 Tool radius comp off 
  lineOfCode.replace(F("G41")," "); // unsupported: G41 Tool radius compensation left   
  lineOfCode.replace(F("G81")," "); // unsupported: G81 Canned drilling cycle 
  lineOfCode.replace(F("G83")," "); // unsupported: G83 Deep hole drilling canned cycle 
  lineOfCode.replace(F("M6")," ");  // ignore Tool change
  lineOfCode.replace(F("M7")," ");  // ignore coolant control
  lineOfCode.replace(F("M8")," ");  // ignore coolant control
  lineOfCode.replace(F("M9")," ");  // ignore coolant control
  lineOfCode.replace(F("M10")," "); // ignore vacuum, pallet clamp
  lineOfCode.replace(F("M11")," "); // ignore vacuum, pallet clamp
  lineOfCode.replace(F("M5")," "); // ignore spindle off
  lineOfCode.replace(F("M2 "),"M5 M2 "); // Shut down spindle on program end.
  
  // Ignore comment lines 
  // Ignore tool commands, I do not support tool changers
    if (lineOfCode.startsWith("(") || lineOfCode.startsWith("T") ) {lineOfCode="";}    

  lineOfCode.trim();
  
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
  Serial.print("!");  // feed hold
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
  int index=0;
  bool completeMessage=false;
  
  checkForOk();

  Serial.print("?");  // Ask the machine status
  while (Serial.available() == 0) { }  // Wait for response 
  while (Serial.available()) {
    content[index] = Serial.read();  
    if (content[index] =='>') completeMessage=true; // a simple check to see if the message is complete
    if (index>0) {if (content[index]=='k' && content[index-1]=='o') {awaitingOK=false;}}
    index++;
    delay(1); 
    }
  content[index] ='\0';
 
  if (!completeMessage) { return; }   
   
  // get the machine status
  int startpos =  strchr(content,'<')-content+1;
  int endpos = strchr(&content[startpos],',')-content;
  memcpy(machineStatus,&content[1],endpos-1  );
  machineStatus[endpos-1]='\0';
 
  // get MposX, using memcpy to be memory efficient.
  startpos =  strchr(content,':')-content+1;
  endpos = strchr(&content[startpos],',')-content;
  memcpy(MposX,&content[startpos],endpos-startpos  );
  MposX[endpos-startpos]='\0';

  // get MposY, using memcpy to be memory efficient.
  startpos =  strchr(&content[endpos],',')-content+1;
  endpos = strchr(&content[startpos],',')-content;
  memcpy(MposY,&content[startpos],endpos-startpos  );
  MposY[endpos-startpos]='\0';

  //get MposZ, using memcpy to be memory efficient.
  startpos =  strchr(&content[endpos],',')-content+1;
  endpos = strchr(&content[startpos],',')-content;
  memcpy(MposZ,&content[startpos],endpos-startpos  );
  MposZ[endpos-startpos]='\0';

  //get WposX, using memcpy to be memory efficient.
  startpos =  strchr(&content[endpos],':')-content+1;
  endpos = strchr(&content[startpos],',')-content;
  memcpy( WposX,&content[startpos],endpos-startpos  );
  WposX[endpos-startpos]='\0';

  //get WposY, using memcpy to be memory efficient.
  startpos =  strchr(&content[endpos],',')-content+1;
  endpos = strchr(&content[startpos],',')-content;
  memcpy( WposY,&content[startpos],endpos-startpos  );
  WposY[endpos-startpos]='\0';

  //get WposZ, using memcpy to be memory efficient.
  startpos =  strchr(&content[endpos],',')-content+1;
  endpos = strchr(&content[startpos],'>')-content;
  memcpy( WposZ,&content[startpos],endpos-startpos  );
  WposZ[endpos-startpos]='\0';


}

void loop() {  
  // We should never get to this point, unless the SD card fails to initialize.
  setTextDisplay(F("You Failed!,"),F("  Press Reset!"),"","");  
  delay(1000);
}

