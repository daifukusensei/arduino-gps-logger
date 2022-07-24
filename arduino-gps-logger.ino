// GPS code from https://randomnerdtutorials.com/guide-to-neo-6m-gps-module-with-arduino/
// LED blinking code from https://learn.adafruit.com/multi-tasking-the-arduino-part-1/using-millis-for-timing
// EEPROM code from https://www.arduino.cc/en/Tutorial/EEPROMWrite, https://www.arduino.cc/en/Reference/EEPROMRead
// Software Serial code from https://www.arduino.cc/en/tutorial/SoftwareSerialExample

#include <Adafruit_SSD1306.h>
#include <EEPROM.h>
#include <NMEAGPS.h>
#include <SdFat.h>
#include <SoftwareSerial.h>
#include <SPI.h>

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define GPSPORT Serial1 // serial port of the connected GPS

#define OLED_RESET -1 //4 // reset pin # (or -1 if sharing Arduino reset pin)
// Nano Every pins:
#define BTENPIN 17 // the number of the bluetooth HC-05 module's EN pin, used for initiating AT mode to re-configure the device
#define BTRXPIN 21 // the number of the bluetooth HC-05 module's RX pin, used for receiving AT commands issued via serial communication
#define BTVCCPIN 16 // the number of the 2N2222 transistor's base pin, used to power on/off the bluetooth HC-05 module
#define BTSTATEPIN 15 // the number of the bluetooth HC-05 module's state pin, used for controlling the bluetooth state LED
#define BTNLED 8 // the number of the LED state-toggle button's pin
#define BTNNEXT 6 // the number of the next page button's pin (used for switching screen pages and toggling bluetooth state)
#define BTNSELECT 5 // the number of the select button's pin (used for changing settings and toggling logging state)
#define BTNMARKER 7 // the number of the waypoint-marker button's pin (used for marking a waypoint)
#define LEDBLUETOOTH 4 // the number of the bluetooth LED
#define LEDGPS 3 // the number of the GPS-fix LED
#define LEDLOG 2 // the number of the logging LED (indicates writes to microSD card)
// Mega2560 pins:
/*#define BTENPIN 23 // the number of the bluetooth HC-05 module's EN pin, used for initiating AT mode to re-configure the device
#define BTRXPIN 22 // the number of the bluetooth HC-05 module's RX pin, used for receiving AT commands issued via serial communication
#define BTVCCPIN 43 // the number of the 2N2222 transistor's base pin, used to power on/off the bluetooth HC-05 module
#define BTSTATEPIN 25 // the number of the bluetooth HC-05 module's state pin, used for controlling the bluetooth state LED
#define BTNNEXT 8 // the number of the next page button's pin (used for switching screen pages)
#define BTNSELECT 7 // the number of the select button's pin (used for changing settings)
#define BTNMARKER 9 // the number of the waypoint-marker button's pin (used for marking a waypoint)
#define LEDBLUETOOTH 6 // the number of the bluetooth LED
#define LEDGPS 5 // the number of the GPS-fix LED
#define LEDLOG 4 // the number of the logging LED (indicates writes to microSD card)*/
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

SoftwareSerial BTSERIAL(20, BTRXPIN); // RX, TX on Arduino board

// define button struct
typedef struct Btn {
  Btn(byte pin); // the physical number of the button's pin
  byte pin; // the physical number of the button's pin
  byte currState = 0; // current state of button press (LOW or HIGH)
  byte lastState = 0; // prior state of button press for comparison during program execution
  boolean ignore = false; // used to help manage button presses
  boolean longPress = false; // indicates when a button was long-pressed, intended for changing settings at next display redraw
  boolean shortPress = false; // indicates when a button was short-pressed, intended for changing settings at next display redraw
  unsigned long millisDown = 0; // milliseconds since a button has first been held
  unsigned long millisUp = 0; // milliseconds since a button has been released
  void checkState(); // method for checking the state of each button
};

// define led class
typedef struct Led {
  Led(byte pin); // the physical number of the LED's pin
  byte pin; // the physical number of the LED's pin
  boolean turnOff = false; // flag to set LED state to LOW (off) on next pass of checkLeds() function
  unsigned long millisLedOnStart = 0; // milliseconds since last time LED was first turned on
  byte millisLedOnMax; // max milliseconds of on-time for LED when blinking
  short int millisLedOffMax; // max milliseconds of off-time for LED when blinking
  void blink(); // method for blinking LED at defined intervals with each pass of checkLeds() function
  boolean longBlink = false; // flag to indicate that LED should remain on for an extended period of time at next blink interval
  void turnOn(); // method for immediately setting LED state to HIGH (on)
};

Btn btnLedState(BTNLED); // create a Btn object for the LED state-toggle button
Btn btnNext(BTNNEXT); // create a Btn object for the next page button (used for switching screen pages)
Btn btnSelect(BTNSELECT); // create a Btn object for the select button (used for changing settings)
Btn btnMarker(BTNMARKER); // create a Btn object for the waypoint-marker button (used for marking a waypoint)
Led ledBluetooth(LEDBLUETOOTH); // create an Led object for the bluetooth LED
Led ledGps(LEDGPS); // create an Led object for the GPS-fix LED
Led ledLog(LEDLOG); // create an Led object for the logging LED (indicates writes to microSD card)

NMEAGPS gps; // GPS object
gps_fix fix; // GPS fix structure
SdFat SD; // microSD card object
File logfile; // file to log to on microSD card

// define constants
// Nano Every pin:
const byte sdCSPin = 14; // the number of the microSD card adapter CS pin
// Mega2560 pin:
//const byte sdCSPin = 53; // the number of the microSD card adapter CS pin
const unsigned short int baud = 4800; // target baud rate of the bluetooth and GPS modules
const short int millisDispRefreshOffMax = 2000; // max milliseconds display refresh/inputs are blocked, for display of system messages
const short int millisBtnLongPressMax = 2000; // max milliseconds before a button long-press is detected
const short int millisLoggingInterval = 2000; // interval between writes to microSD card
const short int millisTextScrollPause = 2000; // max milliseconds text is statically displayed before scrolling
const byte mainPageCount = 7; // number of main screen pages
const byte subPageCount = 6; // number of sub (settings) screen pages
const byte subPageSetTimeoutMin = 5; // minimum allowable value for display timeout
const byte subPageSetTimeoutMax = 30; // maximum allowable value for display timeout

// define global variables
boolean dispOn = false; // used to indicate if the display is currently on
boolean dispRefresh = true; // used to toggle display refreshing & interrupts to allow for display of system messages
boolean sdCardReady = false; // used to indicate if microSD card is ready
boolean sdLogReady = false; // used to indicate if a logfile on microSD has been created and is available for writing
unsigned long millisDispOnStart = 0; // milliseconds since display has been turned on
unsigned long millisDispRefreshOffStart = 0; // milliseconds since display refresh/inputs were first blocked
unsigned long millisLoggingStart = 0; // milliseconds since last time access to the microSD card was initiated
unsigned long millisTextScrollStart = 0; // milliseconds since text scrolling timer started
boolean mainScreenActive = true; // used to indicate if the main screen or sub (settings) screen is active
byte mainPageValue = 0; // used to cycle through available main screen pages
byte subPageValue = 0; // used to cycle through available sub (settings) screen pages
boolean cursorEnterSettings = false; // used to indicate if "Enter" text is selected on main screen settings page
boolean inMeikeHousing = true; // indicates if display is housed in a Meike MK-RC10N flash trigger, with less real estate
short int currX = display.width(); // starting X-coordinate for when text scrolling is needed
char logfileName[] = "yymmdd_log_##.txt"; // dynamic track logging filename on microSD card (based on unique date & number-sequence)
char str1[11]; // maximum number of characters at textsize=2 that can fit on one line of the display, +1 for null terminator
char str2[11];

// main screen page display toggle parameters
byte mainPageSetAltitudeVal = 0; // display altitude in feet, meters, kilometers or miles (default feet)
boolean mainPageSetSpeedKm = true; // display speed in either km/h or mph (default km/h)

// settings screen page parameters
boolean subPageSetBluetoothOn = false; // bluetooth is either OFF or ON (default OFF)
boolean subPageSetLedOn = true; // LEDs are either ON or OFF (default ON)
boolean subPageSetLoggingOn = false; // logging is either OFF or ON (default OFF)
long subPageSetTimeoutVal = 0; // 5-30
boolean subPageSetDefault = false; // used to indicate if next button was pressed on "Defaults" text in sub (settings) screen
boolean subPageSetDone = false; // used to indicate if next button was pressed on "Done" text in sub (settings) screen

void setup() {
  if (! display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // address 0x3D for 128x64
    for (;;); // failed to initialize OLED display so don't proceed; loop forever
  }

  //setDefaults();  // erase EEPROM and set to defaults
  //writeEeprom();
 
  // read EEPROM values into memory
  readEeprom();

  // intialize the LEDs
  ledBluetooth.millisLedOnMax = 100;
  ledBluetooth.millisLedOffMax = 1000;
  ledGps.millisLedOnMax = 100;
  ledGps.millisLedOffMax = 1000;
  ledLog.millisLedOnMax = 100;

  // initialize the display
  display.ssd1306_command(SSD1306_DISPLAYOFF);
  display.setTextColor(SSD1306_WHITE);
  millisDispOnStart = millis(); // start the display on/off state timer

  pinMode(BTENPIN, OUTPUT);
  digitalWrite(BTENPIN, LOW); // bluetooth HC-05 module's EN pin is only used when re-configuring the device, and is thus set to LOW
  pinMode(BTVCCPIN, OUTPUT);
  digitalWrite(BTVCCPIN, LOW); // start with bluetooth HC-05 module off unless user settings (read later from EEPROM) indicate otherwise
  pinMode(BTRXPIN, INPUT); // set Arduino's TX pin (bluetooth HC-05 module's RX pin) to input mode so as to not interfere with GPS
                           // NMEA sentences being sent to the HC-05 module's same RX pin and the same 'line' to Arduino's serial RX--
                           // perhaps one day I'll learn enough to figure out the issue and why input mode works

  // reset the bluetooth HC-05 module's baud rate to our target baud rate should it have been forgotten by holding the
  // waypoint-marker button when powering on-- may require a few attempts
  if (digitalRead(btnMarker.pin) == LOW) {
    ledBluetooth.turnOn(); // immediately turn on the bluetooth LED to indicate attempted setting of baud rate
    setBluetoothBaudRate(baud);
  }

  // initialize the microSD card
  sdInit();

  GPSPORT.begin(9600); // open serial connection with GPS at default baud rate of 9600
  
  setGpsBaudRate(baud); // set the GPS baud rate to 4800 with each startup
  byte compatMode[] = { 0xB5, 0x62, 0x06, 0x17, 0x04, 0x00, 0x00, 0x23, 0x00, 0x03, 0x47, 0x55, 0xE3, 0xC6 };
  sendUBX(compatMode, sizeof(compatMode) / sizeof(byte)); // set the GPS compat flag via a u-blox UBX-CFG-NMEA command to enable
                 // compatibility mode and present co-ordinates in 4 decimal places rather than 5, required for Nikon cameras
}

void loop() {
  checkDisplay(); // manage display refresh & on/off state

  // display specific main screen page
  if (dispRefresh) { // only allow for changes if dispRefresh = true, to allow for display of system messages and user actions
    if (dispOn) { // only refresh the display if it's currently on
      if (btnNext.shortPress) { // next button was short-pressed, so change screen page
        changeScreenPage();
      } else {
        if (mainScreenActive) {
          drawMainScreenPage(); // refresh the selected main screen page if it's active
        } else {
          drawSubScreenPage(); // refresh the selected sub (settings) screen page if it's active
        }
      }
    }
  }

  checkBtns(); // check state of buttons
  checkLeds(); // take action on LEDs (blink, turn off, etc.)
  checkSerialData(); // check incoming serial data and pass to gps object
  checkBluetooth(); // take actions against bluetooth HC-05 module
  checkLogger(); // take logging actions
}

void checkDisplay() {
  // re-enable display refresh if it's been disabled longer than millisDispRefreshOffMax
  if (! dispRefresh) {
    if (millis() - millisDispRefreshOffStart >= (millisDispRefreshOffMax)) {
      dispRefresh = true;
    }
  }

  // turn off display if it's not already off, and if it's been on longer than the defined timeout
  if ((millis() - millisDispOnStart >= (subPageSetTimeoutVal * 1000)) && (dispOn == true)) {
    display.ssd1306_command(SSD1306_DISPLAYOFF);
    dispOn = false;
  }
}

void checkBtns() {
  btnLedState.checkState();
  btnNext.checkState();
  btnSelect.checkState();
  btnMarker.checkState();

  // only take action on long-presses if we're on the main screen, to avoid confusingly overwriting
  // user settings on the settings screen that may not have been intentionally changed & saved yet
  if (mainScreenActive) {
    // toggle LED state
    if (btnLedState.longPress) { // the LED state-toggle button was long-pressed, so take action regardless of display being on/off
      millisDispOnStart = millis();
      display.ssd1306_command(SSD1306_DISPLAYON); // turn on display
      dispOn = true; // indicate that display is on
      display.clearDisplay();
      if (subPageSetLedOn) {
        drawBodyText("LEDs", "OFF!"); // LEDs were enabled, so disable them
      } else {
        drawBodyText("LEDs", "ON!"); // LEDs were disabled, so enable them
      }
      subPageSetLedOn = ! subPageSetLedOn; // toggle LED state and write to EEPROM
      writeEeprom();
      display.display();
      dispRefresh = false; //prevent display refresh and button input to allow display of system messages and user actions
      millisDispRefreshOffStart = millis();
    }
    
    // add a waypoint-marker
    if (btnMarker.longPress) { // the waypoint-marker button was long-pressed, so take action regardless of display being on/off
      millisDispOnStart = millis();
      display.ssd1306_command(SSD1306_DISPLAYON); // turn on display
      dispOn = true; // indicate that display is on
      display.clearDisplay();
      if (sdCardReady) { // attempt to mark a waypoint only if microSD card is accessible
        if (fix.valid.location) {
          ledLog.longBlink = true; // indicate that logging LED should remain on for a brief period of time to visually indicate a
                                   // waypoint was written to file
          sdWriteMarker(); // write a waypoint to file
          drawBodyText("Marked", "waypoint!"); // mark a waypoint only if there's valid GPS co-ordinates  
        } else {
          drawBodyText("No", "position!"); // no valid GPS co-ordinates available in order to mark a waypoint  
        }
      } else {
        drawBodyText("Card", "error!"); // microSD card failed to initialize earlier, so display an error
      }
      display.display();
      dispRefresh = false; //prevent display refresh and button input to allow display of system messages and user actions
      millisDispRefreshOffStart = millis();
    }
  
    // toggle bluetooth state
    if (btnNext.longPress) { // the next button was long-pressed, so take action regardless of display being on/off
      millisDispOnStart = millis();
      display.ssd1306_command(SSD1306_DISPLAYON); // turn on display
      dispOn = true; // indicate that display is on
      display.clearDisplay();
      if (subPageSetBluetoothOn) {
        drawBodyText("Bluetooth", "OFF!"); // bluetooth was enabled, so disable it
      } else {
        drawBodyText("Bluetooth", "ON!"); // bluetooth was disabled, so enable it
      }
      ledBluetooth.longBlink = true; // indicate that bluetooth LED should remain on for a brief period of time to visually indicate
                                     // change of state
      subPageSetBluetoothOn = ! subPageSetBluetoothOn; // toggle bluetooth state and write to EEPROM
      writeEeprom();
      display.display();
      dispRefresh = false; //prevent display refresh and button input to allow display of system messages and user actions
      millisDispRefreshOffStart = millis();
    }
  
    // toggle logging state
    if (btnSelect.longPress) { // the select button was long-pressed, so take action regardless of display being on/off
      millisDispOnStart = millis();
      display.ssd1306_command(SSD1306_DISPLAYON); // turn on display
      dispOn = true; // indicate that display is on
      display.clearDisplay();
      if (sdCardReady) { // attempt to change logging state only if microSD card is accessible
        if (subPageSetLoggingOn) {
          drawBodyText("Logging", "OFF!"); // logging was enabled, so disable it
        } else {
          drawBodyText("Logging", "ON!"); // logging was disabled and microSD card was successfully initialized earlier, so enable logging
        }
        ledLog.longBlink = true; // indicate that logging LED should remain on for a brief period of time to visually indicate change of
                                 // state
        subPageSetLoggingOn = ! subPageSetLoggingOn; // toggle logging state and write to EEPROM
        writeEeprom();
      } else {
        drawBodyText("Card", "error!"); // microSD card failed to initialize earlier, so display an error
      }
      display.display();
      dispRefresh = false; //prevent display refresh and button input to allow display of system messages and user actions
      millisDispRefreshOffStart = millis();
    }
  }
}

void Btn::checkState() {
  // reset button states so as to not continually change settings/screen pages when redrawing with each loop if button is held too long
  longPress = false;
  shortPress = false;

  currState = digitalRead(pin); // get current state of button to determine if it's been pressed
  if (currState == LOW && dispRefresh) { // button is currently DOWN (pressed/held); reset millisDown timer only if
                                         // dispRefresh = true to prevent longPress action of another button from running
                                         // immediately first long-press is complete
    millisDown = millis();
    if (((millisDown - millisUp) >= millisBtnLongPressMax) && (! ignore)) { // take action on long-presses
      longPress = true; // indicate that the button was long-pressed
      ignore = true; // indicate that the button is still held to avoid acting on another long-press with each loop
    }
    // take action on short-presses- this is differentiated from long-presses by the button needing to be in a released state
    // (HIGH) && the current released state differing from a last recorded state of LOW (to avoid the released state being
    // acted on with each loop) && the button not just having been immediately released following an intended long-press
    // (to prevent the short-press action from unintentionally occurring immediatley after a long-press action):
  } else if ((currState == HIGH) && (currState != lastState) && (! ignore)) {
    millisDispOnStart = millis();
    display.ssd1306_command(SSD1306_DISPLAYON); // turn on display
    if (dispOn) { // only do something if display is already on
      shortPress = true;
    } else { // display was off, so first turn it on upon press of button before permitting further action from button
      dispOn = true; // display is on following a recent button press, so permit button actions with subsequent presses
    }
  } else {
    millisUp = millis();
    ignore = false;
  }
    
  lastState = currState; // save state of button press for comparison with each loop
}

void drawMainScreenPage() {
  display.clearDisplay(); // clear display before drawing text
  
  switch (mainPageValue) {
    case 1:
      // display date and time
      drawHeaderText("Date & time (UTC):");

      if (fix.valid.date) { // print date & time only if there's a fix, else print placeholder text
        // format the date as yymmdd and time as military time hhmmss
        sprintf(str1, "%02d%02d%02d", fix.dateTime.year, fix.dateTime.month, fix.dateTime.date);
        sprintf(str2, "%02d%02d%02d", fix.dateTime.hours, fix.dateTime.minutes, fix.dateTime.seconds);
        drawBodyText(str1, str2);
      } else {
        drawBodyText("-");
      }
      break;
      
    case 2:
      // display speed in either kilometers per hour or miles per hour (double)
      drawHeaderText("Speed - press Select to change unit of measurement:");

      if (fix.valid.speed) { // print speed from GPS only if there's a fix, else print placeholder text
        // toggle speed between km/h and mph (default km/h) if select button was short-pressed
        if (btnSelect.shortPress) {
          mainPageSetSpeedKm = ! mainPageSetSpeedKm;
        }
        
        if (mainPageSetSpeedKm) {
          dtostrf(fix.speed_kph(), 0, 0, str1);
          strcat(str1, " km/h"); // append unit of measurement to char array
          drawBodyText(str1);
        } else {
          dtostrf(fix.speed_mph(), 0, 0, str1);
          strcat(str1, " mph");
          drawBodyText(str1);
        }
      } else {
        drawBodyText("-");
      }
      break;

    case 3:
      // display heading in degrees
      drawHeaderText("Heading:");

      if (fix.valid.heading) { // print course only if there's a fix, else print placeholder text
        dtostrf(fix.heading(), 0, 0, str2);
        strcat(str2, " deg");
        if (fix.heading() >= 337 || fix.heading() <= 22) { // N
          drawBodyText("N", str2);
        } else if (fix.heading() <= 67) { // NE
          drawBodyText("NE", str2);
        } else if (fix.heading() <= 112) { // E
          drawBodyText("E", str2);
        } else if (fix.heading() <= 157) { // SE
          drawBodyText("SE", str2);
        } else if (fix.heading() <= 202) { // S
          drawBodyText("S", str2);
        } else if (fix.heading() <= 247) { // SW
          drawBodyText("SW", str2);
        } else if (fix.heading() <= 292) { // W
          drawBodyText("W", str2);
        } else { // NW
          drawBodyText("NW", str2);
        }
      } else {
        drawBodyText("-");
      }
      break;

    case 4:
      // display altitude in feet, meters, kilometers or miles (double)
      drawHeaderText("Altitude - press Select to change unit of measurement:");
      
      if (fix.valid.altitude) { // print altitude only if there's a fix, else print placeholder text
        // toggle altitude between feet, meters, kilometers and miles (default feet) if select button was short-pressed
        if (btnSelect.shortPress) {
          if (mainPageSetAltitudeVal == 3) {
            mainPageSetAltitudeVal = 0;
          } else {
            mainPageSetAltitudeVal++;
          }  
        }
        
        if (mainPageSetAltitudeVal == 0) {
          dtostrf(fix.alt.whole * 3.28084, 0, 0, str1);
          strcat(str1, " ft");
          drawBodyText(str1);
        } else if (mainPageSetAltitudeVal == 1) {
          dtostrf(fix.alt.whole, 0, 0, str1);
          strcat(str1, " m");
          drawBodyText(str1);
        } else if (mainPageSetAltitudeVal == 2) {
          dtostrf(fix.alt.whole * 0.001, 0, 0, str1);
          strcat(str1, " km");
          drawBodyText(str1);
        } else {
          dtostrf(fix.alt.whole * 0.000621371, 0, 0, str1);
          strcat(str1, " mi");
          drawBodyText(str1);
        }
      } else {
        drawBodyText("-");
      }
      break;

    case 5:
      // display number of satellites in use (u32)
      drawHeaderText("Satellites used:");

      if (fix.valid.satellites) { // print sat count only if there's a fix, else print placeholder text
        dtostrf(fix.satellites, 0, 0, str1);
        drawBodyText(str1);
      } else {
        drawBodyText("-");
      }
      break;

    case 6:
      // display Enter settings page:
      drawHeaderText("Settings - press Select then Next to Enter:");

      if (btnSelect.shortPress) {
        cursorEnterSettings = ! cursorEnterSettings; // flip cursor boolean with each press of select button
      }

      if (! cursorEnterSettings) { // invert "Enter" text color when select button is short-pressed
        display.setTextColor(SSD1306_WHITE); // draw white text
      } else {
        display.setTextColor(SSD1306_BLACK, SSD1306_WHITE); // draw inverse text
      }
      drawBodyText("Enter");
      break;
      
    default:
      // display latitude and longitude by default
      drawHeaderText("Latitude & longitude:");

      if (fix.valid.location) { // print co-ordinates only if there's a fix, else print placeholder text
        dtostrf(fix.latitude(), 0, 6, str1); // draw co-ordinates to 6 decimal places
        dtostrf(fix.longitude(), 0, 6, str2);
        drawBodyText(str1, str2); 
      } else {
        drawBodyText("-");
      }
      break;
  }
  display.display();
}

void drawSubScreenPage() {
  display.clearDisplay(); // clear display before drawing text

  switch (subPageValue) {
    case 1:
      // display logging state settings
      drawHeaderText("Logging:");

      // toggle logging state between OFF, ON if select button was short-pressed
      if (btnSelect.shortPress) {
        subPageSetLoggingOn = ! subPageSetLoggingOn;
      }

      if (subPageSetLoggingOn) {
        drawBodyText("ON");
      } else {
        drawBodyText("OFF");
      }
      break;
    
  case 2:
      // display bluetooth state settings
      drawHeaderText("Bluetooth:");

      // toggle bluetooth state between OFF, ON if select button was short-pressed
      if (btnSelect.shortPress) {
        subPageSetBluetoothOn = ! subPageSetBluetoothOn;
      }

      if (subPageSetBluetoothOn) {
        drawBodyText("ON");
      } else {
        drawBodyText("OFF");
      }
      break;

    case 3:
      // display on/off state timeout settings
      drawHeaderText("Timeout:");

      // change display on/off state timeout between 5-30 in increments of 5 if select button was short-pressed
      if (btnSelect.shortPress) {
        if (subPageSetTimeoutVal == subPageSetTimeoutMax) {
          subPageSetTimeoutVal = subPageSetTimeoutMin;
        } else {
          subPageSetTimeoutVal += 5;
        }
      }

      dtostrf(subPageSetTimeoutVal, 0, 0, str1);
      drawBodyText(str1);
      break;

    case 4:
      // display LED state settings
      drawHeaderText("LEDs:");
      
      // toggle LED state between ON, OFF if select button was short-pressed
      if (btnSelect.shortPress) {
        subPageSetLedOn = ! subPageSetLedOn;
      }

      if (subPageSetLedOn) {
        drawBodyText("ON");
      } else {
        drawBodyText("OFF");
      }
      break;

    case 5:
      // display reset EEPROM to defaults setting
      drawHeaderText("Reset to defaults:");

      // change reset EEPROM to defaults selection if select button was short-pressed
      //   Defaults (confirmation to reset parameters to defaults and exit back to main screen page selection
      //   Next (continue cycling though settings screen pages
      if (btnSelect.shortPress) {
        subPageSetDefault = ! subPageSetDefault;
      }

      if (subPageSetDefault) { // invert "Defaults" text color when select button is short-pressed
        display.setTextColor(SSD1306_BLACK, SSD1306_WHITE); // draw inverse text
      } else {
        display.setTextColor(SSD1306_WHITE); // draw white text
      }
      
      drawBodyText("Defaults");
      break;

    default:
      // display done page
      drawHeaderText("Exit & save settings:");

      // change done page selection if select button was short-pressed
      //   Done (confirmation to save and exit back to main screen page selection
      //   Next (continue cycling though settings screen pages
      if (btnSelect.shortPress) {
        subPageSetDone = ! subPageSetDone;
      }

      if (subPageSetDone) { // invert "Done" text color when select button is short-pressed
        display.setTextColor(SSD1306_BLACK, SSD1306_WHITE); // draw inverse text
      } else {
        display.setTextColor(SSD1306_WHITE); // draw white text
      }
      
      drawBodyText("Done");
      break;
  }
  display.display();
}

void changeScreenPage() {
  display.clearDisplay(); // clear display before allowing next screen page to be displayed
  // select button was last used to move the cursor on the main screen's settings page to the
  // "Enter" text, so next button will now cycle through the sub (settings) screen's pages
  if (cursorEnterSettings) {
    mainScreenActive = false; // indicate that the main screen is inactive, to prevent it from redrawing with each system loop
    if ((! subPageSetDone) && (! subPageSetDefault)) { // cycle through available sub (settings) screen pages
      if (subPageValue < subPageCount) {
        subPageValue++;
      } else {
        subPageValue = 1;
      }
      drawSubScreenPage(); // manually draw the selected sub (settings) screen page
    } else { // subPageSetDone indicates "Done" or "Defaults" text in sub (settings) screen was selected,
             // so write new parameters to EEPROM and return to main screen
      writeEeprom();
      cursorEnterSettings = ! cursorEnterSettings; // reset variable
      subPageSetDefault = false; // reset variable
      subPageSetDone = false; // reset variable
      subPageValue = 0; // reset sub (settings) screen page to default value
      mainPageValue = 0; // reset screen page for redrawing during next loop
      mainScreenActive = true; // indicate main screen is now active again to allow redrawing with each system loop
    }
  } else {
    if (mainPageValue < mainPageCount) {  // cycle through main screen pages
      mainPageValue++;
    } else {
      mainPageValue = 1;
    }
  }
  currX = 0; // reset X-coordinate variable to accomodate scrolling header text where needed
  millisTextScrollStart = millis(); // restart the timer for scrolling header text where needed
}

void checkSerialData() {
  // wait for all serial data to be consumed from buffer before passing it to the gps object
  while (gps.available(GPSPORT)) {
    fix = gps.read();
  }
}

void checkBluetooth() {
  digitalWrite(BTVCCPIN, subPageSetBluetoothOn ? HIGH : LOW); // enable or disable HC-05 module, based on user settings
}

void checkLogger() {
  if (subPageSetLoggingOn) {
    if (sdCardReady) {
      if (! sdLogReady) {
        if (fix.valid.date) {
          // create a new date & number-sequenced logfile on microSD card each time the logging state is enabled/re-enabled
          sdCreateLog();
        }
      } else if (millis() - millisLoggingStart >= millisLoggingInterval) {
        // log to microSD card at a specified interval if it's been successfully initialized and a logfile is ready for writing
        sdWriteFile(logfile, logfileName);
      }
    } else {
      subPageSetLoggingOn = false; // disable logging as microSD card is not available
    }
  } else {
    sdLogReady = false; // logging state is disabled, so indicate that no logfile is available for writing
  }
}

void checkLeds() { 
  if (subPageSetLedOn) { // LEDs are enabled
    // GPS-fix LED
    // blink continuously until a GPS-fix is detected, then remain lit
    if (fix.valid.location) {
      digitalWrite(ledGps.pin, HIGH);
    } else {
      ledGps.blink();
    }
  
    // Bluetooth state LED
    // blink if bluetooth is on until pairing is established, then remain lit
    if ((subPageSetBluetoothOn) || (ledBluetooth.longBlink)) { // bluetooth is enabled or longBlink flag was set on bluetooth LED,
                                                               // indicating recent change to state (enabled/disabled)
      if (digitalRead(BTSTATEPIN) == HIGH) { // HC-05 module's state pin indicates a device is paired, so turn on bluetooth LED
        digitalWrite(ledBluetooth.pin, HIGH);
      } else {
        ledBluetooth.blink(); // bluetooth is enabled but no device is yet paired, so blink
      }  
    } else {
      digitalWrite(ledBluetooth.pin, LOW); // bluetooth is disabled, so turn off LED
    }

    // Logging state LED
    if (! sdCardReady) {
      digitalWrite(ledLog.pin, HIGH); // stay on if microSD card is not yet ready, to indicate there's a problem...
    } else if (ledLog.longBlink) {
      ledLog.blink(); // ...else, remain on for a brief period of time if either the waypoint-marker or logging buttons were
                      // long-pressed to add a marker or change logging state respectively, or turn off LED (below) if we've manually
                      // flagged to do so via turnOff boolean
    } else if ((digitalRead(ledLog.pin) == HIGH) && (ledLog.turnOff) && (millis() - ledLog.millisLedOnStart >= ledLog.millisLedOnMax)) {
      ledLog.millisLedOnStart = millis(); // remember the time
      digitalWrite(ledLog.pin, LOW); // update the actual LED
      ledLog.turnOff = false;
    }
  } else {
    ledBluetooth.longBlink = false; // longBlinks are only reset to false on next call of an LED's blink routine, which doesn't occur
                                    // with LEDs off- so set longBlinks to false here should long-presses have occured while LEDs, to
                                    // avoid having them longBlink when LEDs are next turned on
    ledGps.longBlink = false;
    ledLog.longBlink = false;
    digitalWrite(ledBluetooth.pin, LOW);
    digitalWrite(ledGps.pin, LOW);
    digitalWrite(ledLog.pin, LOW);
  }
}

void Led::blink() {
  if ((digitalRead(pin) == HIGH) && (millis() - millisLedOnStart >= (longBlink ? millisLedOnMax * 20 : millisLedOnMax))) {
    millisLedOnStart = millis(); // remember the time
    digitalWrite(pin, LOW); // update the actual LED
    longBlink = false; // exceeded time LED is to remain on, so set longBlink flag to false regardless of it's prior state
  } else if (digitalRead(pin) == LOW) {
    if ((millis() - millisLedOnStart >= millisLedOffMax) || (longBlink)) { // set LED to high if we've exceeded time LED
                                                                           // is to remain off, OR longBlink was flagged
      millisLedOnStart = millis(); // remember the time
      digitalWrite(pin, HIGH); // update the actual LED
    }
  }
}

void Led::turnOn() {
  // function to immediately turn on LED
  if ((subPageSetLedOn) && ! (longBlink)) { // turn on LED only if user has enabled them AND a long-press has not already occurred
                                            // to result in a longBlink, in order to avoid timing issues when blinking it on and off
                                            // from the checkLeds routine- otherwise the logging LED will remain lit longer than
                                            // expected during logging state change 
    digitalWrite(pin, HIGH); // update the actual LED
    millisLedOnStart = millis(); // remember the time
  }
}

void readEeprom() {
  // first check if the used EEPROM address space includes any bytes with values of 255, which is outside the
  // range of our stored values and is indicative of a default Arduino EEPROM state
  for (char eepromAddr = 0; eepromAddr <= 3; eepromAddr++) {
    if (EEPROM.read(eepromAddr) == 255) { // not within range, so set parameters to usable defaults and write to EEPROM
      setDefaults();
      writeEeprom();
      break;
    }
  }

  subPageSetBluetoothOn = EEPROM.read(0); // read index of bluetooth state setting from EEPROM
  subPageSetLedOn = EEPROM.read(1); // read index of LED state setting from EEPROM
  subPageSetLoggingOn = EEPROM.read(2); // read index of logging state setting from EEPROM
  subPageSetTimeoutVal = EEPROM.read(3); // read value of display on/off state timeout setting from EEPROM
}

void writeEeprom() {
  // next button was short-pressed on "Defaults" text in sub (settings) screen, so reset
  // so clear the EEPROM and set some default parameters
  if (subPageSetDefault) {
    setDefaults();
  }

  EEPROM.update(0, subPageSetBluetoothOn); // write index of bluetooth state setting to EEPROM only if changed
  EEPROM.update(1, subPageSetLedOn); // write index of LED state setting to EEPROM only if changed
  EEPROM.update(2, subPageSetLoggingOn); // write index of logging state setting to EEPROM only if changed
  EEPROM.update(3, subPageSetTimeoutVal); // write value of display on/off state timeout setting to EEPROM only if changed
}

void setDefaults() {
  subPageSetBluetoothOn = false;
  subPageSetLedOn = true;
  subPageSetLoggingOn = false;
  subPageSetTimeoutVal = 5;
}

void setBluetoothBaudRate(unsigned short int baud) {
  digitalWrite(BTVCCPIN, LOW); // turn off bluetooth HC-05 module if it's not already off
  delay(100);
  digitalWrite(BTENPIN, HIGH); // set bluetooth HC-05 module to AT mode by first bringing up EN pin before applying power (VCC)
  delay(100);
  digitalWrite(BTVCCPIN, HIGH);
  pinMode(BTRXPIN, OUTPUT); // set Arduino's TX pin (bluetooth HC-05 module's RX pin) to output mode to enable serial communication
  delay(250);

  BTSERIAL.begin(38400); // open serial connection with bluetooth HC-05 module
  delay(250);
  
  BTSERIAL.write("AT\r\n"); // oddly, after toggling BTRXPIN from input to output, an initial AT command needs to first be sent to 'wake
                            // up' the bluetooth HC-05 module, after which subsequent commands will be accepted
  delay(1000); // shorter delays resulted in subsequent AT commands not being acknowledged
  switch(baud) { // send to the bluetooth HC-05 module the required AT command for changing its baud rate
    case 4800:
      BTSERIAL.write("AT+UART=4800,0,0\r\n");
      break;

    case 9600:
      BTSERIAL.write("AT+UART=9600,0,0\r\n");
      break;

    default:
      break;
  }
  delay(1000);

  BTSERIAL.end(); // close serial connection with bluetooth HC-05 module
  digitalWrite(BTENPIN, LOW);
  digitalWrite(BTVCCPIN, LOW); // cut power to bluetooth HC-05 module
  delay(100);
  pinMode(BTRXPIN, INPUT); // set Arduino's TX pin (bluetooth HC-05 module's RX pin) to input mode so as to not interfere with GPS
                           // NMEA sentences being sent to the HC-05 module's same RX pin and the same 'line' to Arduino's serial RX--
                           // perhaps such interference is due to additional voltage leaking over from Arduino when in output mode, thus
                           // malforming the GPS feed? Perhaps one day I'll learn enough to figure out the issue and why input mode works
  digitalWrite(BTVCCPIN, subPageSetBluetoothOn ? HIGH : LOW); // power up bluetooth HC-05 module if needed, based on user settings
  delay(100);
}

void setGpsBaudRate(unsigned short int baud) {
  for (int i = 0; i < 2; i++) {
    delay(100);

    switch(baud) { // send to the u-blox NEO-6M GPS the required NMEA sentence for changing its baud rate
      case 4800:
        GPSPORT.write("$PUBX,41,1,0007,0003,4800,0*13\r\n");
        break;

      case 9600:
        GPSPORT.write("$PUBX,41,1,0007,0003,9600,0*10\r\n");
        break;

      default:
        break;
    }

    delay(100);
    GPSPORT.end();
    delay(100);
    GPSPORT.begin(9600); // we attempt to set the baud rate twice- initially with the connected serial port already open should the GPS
                         // currently be active and accepting of commands, and again with the connected serial port re-opened at the GPS'
                         // default baud rate of 9600 should it have forgotten any previous configuration changes
  }

  delay(100);
  GPSPORT.end();
  delay(100);
  GPSPORT.begin(baud); // re-open the serial port at our desired baud rate
}

void sendUBX(byte *command, byte len) {
  // compat mode command courtesy of: https://www.navilock.com/service/fragen/gruppe_59_uCenter/beitrag/101_ublox-Center-Compatibility-Mode-since-ublox-6.html
  // backup mode command courtesy of: https://forum.arduino.cc/index.php?topic=497410.0
  //                                  https://ukhas.org.uk/guides:ublox_psm
  // checksum calculation courtesy of: https://gist.github.com/tomazas/3ab51f91cdc418f5704d

  for (byte i = 0; i < len; i++) {
    GPSPORT.write(command[i]);
  }
}

void drawHeaderText(char text[]) {
  short int minX; // the minimum X-coordinate before text scrolling restarts from right-edge of display
  
  display.setTextColor(SSD1306_WHITE); // draw white text
  display.setTextSize(1);

  if (strlen(text) > 21) { // scroll text if its length is beyond the display width
    minX = -6 * strlen(text); // 6 pixels/character * text size- 6 for text size of 1 or 12 for text size of 2
    display.setTextWrap(false); // disable text wrapping so string may extend beyond display width
    display.setCursor(currX, inMeikeHousing ? 11 : 0);
    display.print(text);
    display.setTextWrap(true); // re-enable text wrapping now that string is printed, to prevent issues with drawing of other text
    if ((currX == 0) && (millis() - millisTextScrollStart >= millisTextScrollPause)) { // pause until max time is exceeded
      currX = currX - 1; // change X-coordinate variable to text scrolling may commence
    } else if (currX != 0) { // continuous text scrolling until screen page is changed
      if (currX < minX) { // restart text scrolling from right-edge of display
        currX = display.width();
      }
      currX = currX - 1; // scroll speed
    }
  } else {
    drawCentreString(text, 64, inMeikeHousing ? 11 : 0);
  }
  // offset text by approximately 11 pixels vertically to account for smaller housing of Meike flash trigger
  display.drawLine(0, inMeikeHousing ? 22 : 11, display.width(), inMeikeHousing ? 22 : 11, SSD1306_WHITE); // draw header line
}

void drawBodyText(char text[]) {
  display.setTextSize(2);
  // offset text vertically to account for smaller housing of Meike flash trigger
  drawCentreString(text, 64, inMeikeHousing ? 38 : 33);
}

void drawBodyText(char text1[], char text2[]) {
  display.setTextSize(2);
  // offset text vertically to account for smaller housing of Meike flash trigger
  drawCentreString(text1, 64, inMeikeHousing ? 29 : 24);
  drawCentreString(text2, 64, inMeikeHousing ? 47 : 44);
}

void drawCentreString(const char *buf, int x, int y) {
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(buf, 0, y, &x1, &y1, &w, &h); //calc width of new string
  display.setCursor(x - w / 2, y);
  display.print(buf);
}

void sdInit() {
  // see if the microSD card is present and can be initialized
  if (SD.begin(sdCSPin)) {
    sdCardReady = true; // indicate we're ready to begin logging
  }
}

void sdCreateLog() {
  // pick a new date & number-sequenced filename with each power cycle
  sprintf(logfileName, "%02d%02d%02d", fix.dateTime.year, fix.dateTime.month, fix.dateTime.date); // get current date from GPS
  strcat(logfileName, "_log_##.txt"); // append _##.txt to filename*/

  for (uint8_t i = 0; i < 100; i++) {
    logfileName[11] = '0' + i/10;
    logfileName[12] = '0' + i%10;
    if (! SD.exists(logfileName)) {
      // use this one!
      break;
    }
  }

  logfile = SD.open(logfileName, FILE_WRITE);
  if (logfile) {
    ledLog.turnOn(); // turn on logging LED immediately
    //logfile.println( F("Latitude,Longitude,Speed (km/h),Heading (degrees),Altitude (floating-point meters),Date and Time (UTC)") );
    logfile.println("Latitude,Longitude,Speed (km/h),Heading (degrees),Altitude (floating-point meters),Date and Time (UTC)");
    logfile.flush(); // close file after each write as only a single file can be open at a time
    ledLog.turnOff = true; // indicate that logging LED should be turned off at next pass of checkLeds() function
    
    sdLogReady = true; // indicate that a new logfile is available for writing
  } else {
    sdCardReady = false; // there was a problem creating a new logfile, so set microSD card ready state to false to visually report a problem
    subPageSetLoggingOn = false; // disable logging as microSD card is not available
  }
}

void sdWriteMarker() {
  File waypointfile;
  char waypointfileName[] = "waypoints.txt";
  
  if (! SD.exists(waypointfileName)) { // create waypoints.txt file if it doesn't already exist
    waypointfile = SD.open(waypointfileName, FILE_WRITE);
    if (waypointfile) {
      ledLog.turnOn(); // turn on logging LED immediately
      //waypointfile.println( F("Latitude,Longitude,Speed (km/h),Heading (degrees),Altitude (floating-point meters),Date and Time (UTC)") );
      waypointfile.println("Latitude,Longitude,Speed (km/h),Heading (degrees),Altitude (floating-point meters),Date and Time (UTC)");
      waypointfile.flush(); // close file after each write as only a single file can be open at a time
      ledLog.turnOff = true; // indicate that logging LED should be turned off at next pass of checkLeds() function
    } else {
      sdCardReady = false; // there was a problem creating a new waypoints file, so set microSD card ready state to false to visually report a problem
      subPageSetLoggingOn = false; // disable logging as microSD card is not available
    }
  }

  sdWriteFile(waypointfile, waypointfileName);
}

void sdWriteFile(File file, const char *filename) {
  if (fix.valid.location) {
    file = SD.open(filename, FILE_WRITE);
    if (file) {
      ledLog.turnOn(); // turn on logging LED immediately
      printL( file, fix.latitudeL() ); // write current latitude to file
      file.print( ',' );
      
      printL( file, fix.longitudeL() ); // write current longitude to file
      file.print(',');
      
      printL( file, fix.speed_kph() ); // write current speed in km/h to file
      file.print(',');
  
      printL( file, fix.heading() ); // write current heading in degrees to file
      file.print(',');
  
      printL( file, fix.altitude() ); // write current altitude in meters to file
      file.print(',');
  
      if (fix.dateTime.hours < 10) // write current date & time in UTC format to file
        file.print( '0' );
      file.print(fix.dateTime.hours);
      file.print( ':' );
      if (fix.dateTime.minutes < 10)
        file.print( '0' );
      file.print(fix.dateTime.minutes);
      file.print( ':' );
      if (fix.dateTime.seconds < 10)
        file.print( '0' );
      file.print(fix.dateTime.seconds);
      file.print( '.' );
      if (fix.dateTime_cs < 10)
        file.print( '0' ); // leading zero for .05, for example
      file.print(fix.dateTime_cs);
  
      file.println();
      
      file.flush(); // close file
      ledLog.turnOff = true; // indicate that logging LED should be turned off at next pass of checkLeds() function
      millisLoggingStart = millis(); // remember the time
    } else {
      sdCardReady = false; // there was a problem writing to file, so set microSD card ready state to false to visually report a problem
      subPageSetLoggingOn = false; // disable logging as microSD card is not available
    }
  }
}

void printL(Print & outs, int32_t degE7) {
  // extract and print negative sign
  if (degE7 < 0) {
    degE7 = -degE7;
    outs.print( '-' );
  }

  // whole degrees
  int32_t deg = degE7 / 10000000L;
  outs.print( deg );
  outs.print( '.' );

  // get fractional degrees
  degE7 -= deg*10000000L;

  // print leading zeroes, if needed
  if (degE7 < 10L)
    //outs.print( F("000000") );
    outs.print("000000");
  else if (degE7 < 100L)
    //outs.print( F("00000") );
    outs.print("00000");
  else if (degE7 < 1000L)
    //outs.print( F("0000") );
    outs.print("0000");
  else if (degE7 < 10000L)
    //outs.print( F("000") );
    outs.print("000");
  else if (degE7 < 100000L)
    //outs.print( F("00") );
    outs.print("00");
  else if (degE7 < 1000000L)
    //outs.print( F("0") );
    outs.print("0");
  
  // print fractional degrees
  outs.print( degE7 );
}

// description of the Btn struct constructor
Btn::Btn(byte p) {
  pin = p;
  pinMode(pin, INPUT_PULLUP); // set pin as input
}

// description of the Led struct constructor
Led::Led(byte p) {
  pin = p;
  pinMode(pin, OUTPUT); // set pin as output
  digitalWrite(pin, LOW);
}
