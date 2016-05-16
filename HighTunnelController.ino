#include <Time.h>
#include <SD.h>
#include <WildFire.h>
#include <Wire.h>
// #include "RTClib.h"

// Constants used by program, note that the file names are essentially constants but specifying that causes an error
const unsigned long MIN_ALLOWABLE_DATE_TIME = 1420156799; // 1 second before Jan 1 2015 GMT
const unsigned long MAX_ALLOWABLE_DATE_TIME = 1893520800; // Jan 1 2030 GMT
const unsigned long SECONDS_IN_HOUR         = 3600;
char configFileName[] = "live.cfg"; // recommended to leave this set and rename a different file as live.cfg to update it
char logFileName[] = "live.log"; // recommended to leave this set.  Multiple log files will be spun off as we go.
char heatWaveFileName[] = "heat.log"; // recommended to leave this set. This is where heatwave data is logged explicitly.

// External variables that can be passed in via config file
int gmt_hour_offset = -5; // -5 is the EST offset
float too_hot_delta = 6.0;  // If difference between inside and outside temp is more than this start to cool
float way_too_hot_delta = 7.5; // If difference between inside and outside temp is more than this use more aggressive cooling measures
float too_cool_delta = 4.0; // If difference between inside and outside temp is less than this start to warm
float super_cool_delta = -1.0; // If difference between inside and outside temp is less than this and warm up inside using outside air
boolean disable_log_file = false; // setting this to true disables writing to log file. Can help debug memory card issues
boolean manual_sensor_entry_mode = false; // If true then read sensors from Serial input by user instead of actual sensors.  Used to test system.
char temperature_units = 'F'; // Set ot F for Farenheight of C for Celcius.  If you change this you should probably change the deltas too.
int heat_wave_temperature = 90; // Temperature at which tunnel is considered a heatwave. WARNING if you update temperature units to C you MUST update this as well
int heat_wave_enabled_warming_temperature = 95; // When heat wave is enabled keep warming tunnel till it hits this. Should be enough above heat_wave_temperature to ensure it doesm't cool down below it
int heat_wave_enabled_cooling_temperature = 99; // When heat wave is enabled cool if tunnel gets above this. If outside is hot the keep up with that temp instead
int heat_wave_hours_to_log = 6; // How many consecutive hours temp must be above heat_wave_temperature to count as a heat wave day
int heat_wave_days_to_log = 3; // How many consecutive heat wave days must happen to count as a heat wave
int heat_wave_switch_pin = 11; // Pin the switch to enable heat wave mode is connected to
int pir_sensor_pin = 12; // Pin that PIR motion sensors that ensure no one is around while sides roll are connected to.
int pir_wait_time_max_seconds = 60; //If PIR sensors detect someone this is how long we'll wait before we give up and accept that we cannot roll
int pir_milliseconds_between_checks = 1000; //How long to wait between sampling the PIR sensors again when someone is detected
int inside_temp_sensor_pin = A3; // Analog pin that temperature sensor inside the high tunnel is connected to
int outside_temp_sensor_pin = A0; // Analog pin that temperature sensor outside the high tunnel is connected to
unsigned long millisecond_delay_between_actions = 5000; // How long to wait between reading temperature and taking action to correct it
int winch_roll_seconds = 5; // How many seconds to roll each winch at a time
int winch_roll_milliseconds_between_limit_check = 100; // How long to wait between checking limit sensors while rolling
int fan_run_seconds = 20; // How how long to run fans at a stretch
int east_winch_roll_direction_digital_pin = 3; // Digital pin that sets direction east winch will roll, also sounds alarm when powered up
int east_winch_roll_power_digital_pin = 4; // Digital pin that causes east winch to start rolling
int west_winch_roll_direction_digital_pin = 5; // Digital pin that sets direction west winch will roll, also sounds alarm when powered up
int west_winch_roll_power_digital_pin = 6; // Digital pin that causes west winch to start rolling
int east_winch_limit_pin = A1; // Analog pin that connects to top limit sensor for east winch
int west_winch_limit_pin = A2; // Analog pin that connects to top limit sensor for west winch
int south_shutters_direction_digital_pin = 7; // Digital pin that controls whether south shutters open or close
int south_shutters_power_digital_pin = 8; // Digital pin that powers south shutters to move open or closed
int north_shutters_direction_digital_pin = 9; // Digital pin that controls whether north shutters open or close
int north_shutters_power_digital_pin = 10; // Digital pin that powers north shutters to move open or closed
int fan_power_digital_pin = 2; // Digital pin that powers up the fans to spin
int solar_sensor_pin = A4; // Analog pin that senses how much power the solar panel is delivering
boolean automatically_start_high_tunnel_control = false; // Set this to true so that upon restart the high tunnel control program automatically runs

// Internal variables that the program manages itself
boolean sdCardWorking = false;
boolean runFansNext = true; // Used to alternate between running fans and rolling sides
boolean shuttersOpen = false; // Used to track shutter status
int heatWaveRequiredEndTime = 0; // Store the hour/minute when you'll know a heatwave has lasted long enough to be counted for the day
boolean heatWaveTempSurpassedToday = false; // If we ever go above heat wave temp for a moment today note it
boolean heatWaveHoursSurpassedToday = false; // If it has stayed above heat_wave_temperature for heat_wave_hours_to_log then note it
int heatWaveDaysInARow = 0; // Count how many days we have had heatWaveHoursSurpassedToday in a row
int heatWaveVarsResetDay = 0; // Note the last day that heatwave vars were reset, if it isn't today log heatwave data and reset vars
boolean sd_write_enabled = true; // Debugging variable that can be used to temporarily disable writing to SD card
WildFire wf;
// RTC_DS1307 rtc;

////////////////////////Start SD Card Functions////////////////////////
void setupSdCard() {
  Serial.print(F("SD Card Initializating..."));        
  if (!SD.begin(16)) {
    Serial.println(F("SD Init Failed!"));
    sdCardWorking = false;
  }
  else{
    Serial.println(F("SD Init Complete."));
    sdCardWorking = true;
  }
}

void ensureLogFileExists (char sdFileName[]) {
  Serial.println("Ensuring file exists on SD Card.  Filename=" + String(sdFileName));
  File myFile = SD.open(sdFileName, FILE_READ);
  if (myFile) {
    myFile.close();
    logMessage("File found: " + String(sdFileName));
  }
  else {
    logMessage("File not found: " + String(sdFileName));
    writeSdFile("Initializing File: " + String(sdFileName), sdFileName);
  }
  myFile = SD.open(sdFileName, FILE_READ);
  if (!myFile)
    logError("Problem creating file: " + String(sdFileName));
  myFile.close();
}

void readConfigFromFile () {
  File configFile;
  configFile = SD.open(configFileName, FILE_READ);
  if (configFile) {
    logMessage("Reading config file " + String(configFileName));
      
    // read key value pairs from the file until there's nothing else in it:
    while (configFile.available()) {
      String key = configFile.readStringUntil('=');
      String value = configFile.readStringUntil('\n');
      processKeyValuePair(key,value,false);
    }
    // close the file:
    configFile.close();
  } else 
    logMessage("Error opening config file " + String(configFileName));
}

int processConfigInt (String key, String value) {
  float output = value.toInt();
  logMessage("Config File setting " + key + " to int " + String(output));
  return output;
}

float processConfigFloat (String key, String value) {
  float output = value.toFloat();
  logMessage("Config File setting " + key + " to float " + String(output));
  return output;
}

boolean processConfigBool (String key, String value) {
  boolean output = confirmValidBool(value, true, false);
  logMessage ("Config File setting " + key + " to boolean " + String(output)); 
  return output;
}

boolean validKey (String key, String desiredKey, boolean printOutMode) {
  if (printOutMode) {
    Serial.print (desiredKey + " is a valid key");
    return true;
  }
  if (key == desiredKey)
    return true;
  return false;
}

// printOutMode is used to print a list of keys and what qualifies as valid for each to the terminal for reference.
// coding it this way means that the list of keys and validity is printed directly from this procedure so as new
// keys are added or validity conditions are updated it will always print out correct output.
void processKeyValuePair(String key, String value, boolean printOutMode) {
  if (!printOutMode)
    logMessage("Processing key value pair of key=" + key + " value=" + value);
  if (validKey(key, "gmt_hour_offset", printOutMode) && confirmValidNum(value,true,false, printOutMode))
    gmt_hour_offset = processConfigInt(key,value);
  else if (validKey(key, "too_hot_delta", printOutMode) && confirmValidNum(value,false,true, printOutMode))
    too_hot_delta = processConfigFloat(key,value);
  else if (validKey(key, "way_too_hot_delta", printOutMode) && confirmValidNum(value,false,true, printOutMode))
    way_too_hot_delta = processConfigFloat(key,value);
  else if (validKey(key, "too_cool_delta", printOutMode) && confirmValidNum(value,false,true, printOutMode))
    too_cool_delta = processConfigFloat(key,value);
  else if (validKey(key, "super_cool_delta", printOutMode) && confirmValidNum(value,true,true, printOutMode))
    super_cool_delta = processConfigFloat(key,value);
  else if (validKey(key, "inside_temp_sensor_pin", printOutMode) && confirmValidNum(value,false,false, printOutMode))
    inside_temp_sensor_pin = processConfigInt(key,value);
  else if (validKey(key, "outside_temp_sensor_pin", printOutMode) && confirmValidNum(value,false,false, printOutMode))
    outside_temp_sensor_pin = processConfigInt(key,value);
  else if (validKey(key, "disable_log_file", printOutMode) && confirmValidBool(value, false, printOutMode))
    disable_log_file = processConfigBool(key,value);
  else if (validKey(key, "manual_sensor_entry_mode", printOutMode) && confirmValidBool(value, false, printOutMode))
    manual_sensor_entry_mode = processConfigBool(key,value);
  else if (validKey(key, "temperature_units", printOutMode) && confirmValidTemperatureUnit(value, printOutMode))
    temperature_units = value.charAt(0);
  else if (validKey(key, "heat_wave_temperature", printOutMode) && confirmValidNum(value,false,false, printOutMode))
    heat_wave_temperature = processConfigInt(key,value);
  else if (validKey(key, "heat_wave_enabled_warming_temperature", printOutMode) && confirmValidNum(value,false,false, printOutMode))
    heat_wave_enabled_warming_temperature = processConfigInt(key,value);
  else if (validKey(key, "heat_wave_enabled_cooling_temperature", printOutMode) && confirmValidNum(value,false,false, printOutMode))
    heat_wave_enabled_cooling_temperature = processConfigInt(key,value);
  else if (validKey(key, "heat_wave_hours_to_log", printOutMode) && confirmValidNum(value,false,false, printOutMode))
    heat_wave_hours_to_log = processConfigInt(key,value);
  else if (validKey(key, "heat_wave_days_to_log", printOutMode) && confirmValidNum(value,false,false, printOutMode))
    heat_wave_days_to_log = processConfigInt(key,value);
  else if (validKey(key, "heat_wave_switch_pin", printOutMode) && confirmValidNum(value,false,false, printOutMode))
    heat_wave_switch_pin = processConfigInt(key,value);
  else if (validKey(key, "pir_sensor_pin", printOutMode) && confirmValidNum(value,false,false, printOutMode))
    pir_sensor_pin = processConfigInt(key,value);
  else if (validKey(key, "pir_wait_time_max_seconds", printOutMode) && confirmValidNum(value,false,false, printOutMode))
    pir_wait_time_max_seconds = processConfigInt(key,value);
  else if (validKey(key, "pir_milliseconds_between_checks", printOutMode) && confirmValidNum(value,false,false, printOutMode))
    pir_milliseconds_between_checks = processConfigInt(key,value);    
  else if (validKey(key, "millisecond_delay_between_actions", printOutMode) && confirmValidNum(value,false,false,printOutMode))
    millisecond_delay_between_actions = processConfigInt(key,value);
  else if (validKey(key, "winch_roll_seconds", printOutMode) && confirmValidNum(value,false,false,printOutMode))
    winch_roll_seconds = processConfigInt(key,value);
  else if (validKey(key, "winch_roll_milliseconds_between_limit_check", printOutMode) && confirmValidNum(value,false,false,printOutMode))
    winch_roll_milliseconds_between_limit_check = processConfigInt(key,value);
  else if (validKey(key, "fan_run_seconds", printOutMode) && confirmValidNum(value,false,false,printOutMode))
    fan_run_seconds = processConfigInt(key,value);
  else if (validKey(key, "east_winch_roll_direction_digital_pin", printOutMode) && confirmValidNum(value,false,false,printOutMode))
    east_winch_roll_direction_digital_pin = processConfigInt(key,value);
  else if (validKey(key, "east_winch_roll_power_digital_pin", printOutMode) && confirmValidNum(value,false,false,printOutMode))
    east_winch_roll_power_digital_pin = processConfigInt(key,value);
  else if (validKey(key, "west_winch_roll_direction_digital_pin", printOutMode) && confirmValidNum(value,false,false,printOutMode))
    west_winch_roll_direction_digital_pin = processConfigInt(key,value);
  else if (validKey(key, "west_winch_roll_power_digital_pin", printOutMode) && confirmValidNum(value,false,false,printOutMode))
    west_winch_roll_power_digital_pin = processConfigInt(key,value);
  else if (validKey(key, "east_winch_limit_pin", printOutMode) && confirmValidNum(value,false,false,printOutMode))
    east_winch_limit_pin = processConfigInt(key,value);
  else if (validKey(key, "west_winch_limit_pin", printOutMode) && confirmValidNum(value,false,false,printOutMode))
    west_winch_limit_pin = processConfigInt(key,value);
  else if (validKey(key, "south_shutters_direction_digital_pin", printOutMode) && confirmValidNum(value,false,false,printOutMode))
    south_shutters_direction_digital_pin = processConfigInt(key,value);
  else if (validKey(key, "south_shutters_power_digital_pin", printOutMode) && confirmValidNum(value,false,false,printOutMode))
    south_shutters_power_digital_pin = processConfigInt(key,value);
  else if (validKey(key, "north_shutters_direction_digital_pin", printOutMode) && confirmValidNum(value,false,false,printOutMode))
    north_shutters_direction_digital_pin = processConfigInt(key,value);
  else if (validKey(key, "north_shutters_power_digital_pin", printOutMode) && confirmValidNum(value,false,false,printOutMode))
    north_shutters_power_digital_pin = processConfigInt(key,value);
  else if (validKey(key, "fan_power_digital_pin", printOutMode) && confirmValidNum(value,false,false,printOutMode))
    fan_power_digital_pin = processConfigInt(key,value);
  else if (validKey(key, "solar_sensor_pin", printOutMode) && confirmValidNum(value,false,false,printOutMode))
    solar_sensor_pin = processConfigInt(key,value);
  else if (validKey(key, "automatically_start_high_tunnel_control", printOutMode) && confirmValidBool(value, false, printOutMode))
    automatically_start_high_tunnel_control = processConfigBool(key,value);
  else if (!printOutMode)
    logMessage("key:'" + key + "' is not valid.  Ignoring value:'" + value + "'");
}

boolean confirmValidTemperatureUnit(String str, boolean printOutMode) {
  if (printOutMode)
    Serial.println (" its value must be F or C");
  if (str.charAt(0) == 'F' or str.charAt(0) == 'C')
    return true;
  return false;
}

// returnParsedValue lets this function return the actual parsed boolean value of the string
// this allows us to avoid duplicating the boolean string processing code in processConfigBool which could lead to errors
boolean confirmValidBool(String str, boolean returnParsedValue, boolean printOutMode) {
  if (printOutMode)
    Serial.println (" its value must be a boolean (true, false, t, f, 1, or 0)");
  if (str.equalsIgnoreCase("true") or str.equalsIgnoreCase("t") or str.equalsIgnoreCase("1"))
    return true;
  if (str.equalsIgnoreCase("false") or str.equalsIgnoreCase("f") or str.equalsIgnoreCase("0")) {
    if (returnParsedValue)
      return false;
    else
      return true;
  }
  return false;
}

boolean confirmValidNum(String str, boolean allowNegative, boolean allowDecimalPoint, boolean printOutMode) {
  if (printOutMode) {
    if (allowDecimalPoint)
      Serial.print (" its value must be a decimal number (exe. 2.34)");
    else
      Serial.print (" its value must be an integer number (exe. 2)");
    if (!allowNegative)
      Serial.print (" at or above zero");
    Serial.println();
  }
  boolean isValidNum=false;
  boolean oneDecimalFound=false;
  int i = 0;
  // if we want to allow negatives and the first char is a minus sign ignore it, if we do not want to allow negatives log specific error
  if (str.charAt(0) == '-') 
    if (allowNegative)
      i=1;
    else {
      logError("Negative values not allowed!  Value " + str + " is invalid");
      return false;
    }
  for(i;i<str.length();i++)
  {
    isValidNum = isDigit(str.charAt(i));
    if(!isValidNum)
    {
      // optionally allow on decimal point in string, but it cannot be the only character in the string
      if (allowDecimalPoint && str.length()!=1 && str.charAt(i) == '.') {
        if (oneDecimalFound) {
          logError("Second . found in string " + str + " that we're trying to convert to numeric");
          return false;
        }
        oneDecimalFound = true;
        isValidNum = true;
      }
      else {
        logError("Non digit " + String(str.charAt(i)) + " at position " + String(i) + " in string that should be numeric");
        return false;
      }
    }
  }
  return isValidNum;
}

void writeSdFile(String sdMessage, char sdFileName[]) {
  File myFile;
  Serial.print("Writing to file " + String(sdFileName));
  Serial.println(" Message: " + sdMessage);
  if (sd_write_enabled) {
    myFile = SD.open(sdFileName, FILE_WRITE);
    if (myFile) {
      myFile.println(sdMessage);
      myFile.close();
    }
    else 
      Serial.println("WRITE ERROR: Issue opening file for writing!");
  }
}

void readSdFile(char sdFileName[])
{
  File myFile;
  logMessage("Reading file to Serial: " + String(sdFileName));
  myFile = SD.open(sdFileName, FILE_READ);
  if (myFile) {
    while (myFile.available()) {
      Serial.write(myFile.read());
    }
    myFile.close();
  }
  else
    logError("Issue opening file for reading!");
}

void emptySdFile(char sdFileName[], String confirmationString){
  File myFile;
  if (confirmationString.equals("Y")) {
    logMessage("Emptying data from file " + String(sdFileName));
    SD.remove(sdFileName);
    // After file has been deleted recreate an empty version of it.
    myFile = SD.open(sdFileName, FILE_WRITE);
    myFile.close();
  }
  else
    Serial.println("You tried to enter a command to empty a file but did not include the required Y after the command name");
}

void printFilesInBaseSdDirectory() {
  logMessage("Printing files in base SD directory to Serial");
  File dir = SD.open("/");
  while (true) {
    File entry =  dir.openNextFile();
    if (! entry) {
      // no more files
      break;
    }
    Serial.print(entry.name());
    if (!entry.isDirectory()) {
      Serial.print("\t");
      Serial.println(entry.size(), DEC);
    }
    entry.close();
  }
  dir.close();
}
////////////////////////End SD Card Functions////////////////////////

////////////////////////Start Log and Serial Interface Functions////////////////////////
// method for logging errors.  Ensures they are all prefaced with the same text and can be modified for easier debugging
void logError(String message) {
  logMessage("ERROR: " + message);
}

void logMessage(String message) {
  if (disable_log_file)
    Serial.println(getTimeStampString() + "- UNLOGGED -" + message);
  else
    writeSdFile(getTimeStampString() + "-" + message, logFileName);
}

void logHeatWave(String message) {
  writeSdFile(getTimeStampString() + "-" + message, heatWaveFileName);
}

void printHelp() {
  Serial.println("Acceptable commands via the Serial Monitor");
  Serial.println("If you omit the ; it will wait a second before processing");
  Serial.println("wValue; Write Value to a new line in the config file");
  Serial.println("r; Read all lines from config file to screen");
  Serial.println("eY; Remove all data from config file");
  Serial.println("s; Set key value pairs using values in config file");
  Serial.println("p; Print all allowed key value pairs and allowable inputs for each");
  Serial.println("l; Read all data from log file");
  Serial.println("cY; Remove all data from log file");
  Serial.println("f; Print all the files and folders in base dir");
  Serial.println("TValue; Set RTC time to Value (given as unix timestamp)");
  Serial.println("t; Print currently set RTC date/time to Serial");
  Serial.println("m[ewsf][ud]; Test run a specific motor (east, west, shutters or fans). East and West require a specific direction (up or down)"); 
  Serial.println("a; Check all sensors and log their values");
  Serial.println("b; Begin running the high tunnel temperature control program");
  Serial.println("h; Print this help text");
  Serial.println();
  Serial.println("Waiting for input...");
}

void processSerialInput (char serialCommand){
  String inputValue = Serial.readStringUntil(';');
  
  switch (serialCommand) {
    case 'r':
      readSdFile(configFileName);
      break;
    case 'w':
      writeSdFile(inputValue, configFileName);
      break;
    case 'e':
      emptySdFile(configFileName, inputValue);
      break;
    case 's':
      readConfigFromFile();
      break;
    case 'p':
      processKeyValuePair("", "", true);
      break;
    case 'l':
      readSdFile(logFileName);
      break;
    case 'c':
      emptySdFile(logFileName, inputValue);
      break;
    case 'f':
      printFilesInBaseSdDirectory();
      break;
    case 'T':
      setTimeFromSerialPort(inputValue);
      break;
    case 't':
      Serial.println(getTimeStampString());
      break;
    case 'm':
      testMotors(inputValue);
      break;  
    case 'a':
      readAllSensors();
      break;
    case 'b':
      Serial.println("Running high tunnel control loop.");
      char cancelByte;
      while (cancelByte != 'x') {
        manageHighTunnelTemp();
        Serial.println("Enter x to quit out of high tunnel control loop and return to serial interface");
        delay(millisecond_delay_between_actions);
        if (Serial.available() > 0) {
          cancelByte = Serial.read();
        }
      }
      Serial.print("Cancel Command received returning to serial interface.");
      printHelp();
      break;
    case 'h':
      printHelp();
      break;
  }
}

void testMotors (String inputString) {
  if (inputString == "f")
    runFans();
  else if (inputString == "eu")
    rollSide ("East", "Up");
  else if (inputString == "ed")
    rollSide ("East", "Down");
  else if (inputString == "wu")
    rollSide ("West", "Up");
  else if (inputString == "wd")
    rollSide ("West", "Down");
  else if (inputString == "s")
    changeShutters ();
  else
    Serial.println("Bad input! Allowable values are f,eu,ed,wu,d, and s.");
}

void readAllSensors () {
  limitSwitchHit ("Up", "East", true);
  limitSwitchHit ("Down", "East", true);
  limitSwitchHit ("Up", "West", true);
  limitSwitchHit ("Down", "West", true);
  getTempFromSensor ("inside", true);
  getTempFromSensor ("outside", true);
  checkSolarLevel ();
  isHeatWaveSwitchOn();
  isPersonNearRollers();
}
////////////////////////End Log and Serial Interface Functions////////////////////////


////////////////////////Start Time Functions////////////////////////
void setTimeFromSerialPort(String inputString) {
  unsigned long parsedTime = 0L;

  // Warn user if they entered a bad value
  if (inputString == "") {
    logError("You must provide a value after T");
    logMessage("Get current Unix Epoch value and enter it. exe. T1234567;");
    return;
  }
  
  logMessage("Read time from serial as: " + inputString);
  parsedTime = inputString.toInt();
  // Only set time if it is in a reasonable time range (between 2015 and 2030)
  if(parsedTime > MIN_ALLOWABLE_DATE_TIME && parsedTime < MAX_ALLOWABLE_DATE_TIME) {
    logMessage("Updating time.");
//    rtc.adjust(parsedTime + gmt_hour_offset * SECONDS_IN_HOUR);
    logMessage("Time set");
  }
  else {
    logMessage("Date was not between Jan 1 2015 and Jan 1 2030.  This seems crazy try again.");
  }
}

void timeHelp ()
{
  Serial.println();
  Serial.println("-----How to set the time-----");
  Serial.println("NOTE: The RTC should keep the time unless its battery dies, if so replace it and reset the time");
  Serial.println("Get current unix timestamp (also called Epoch time, meaning seconds since Jan 1 1970)");
  Serial.println("On a mac or unix system simply type date +%s into the terminal");
  Serial.println("http:// www.epochconverter.com/ also has it");
  Serial.println("Then enter it into Serial Monitor prefaced by a T and followed by a ;");
  Serial.println("Example: T1420156799; is 1 second before Jan 1 2015 GMT");
  Serial.println("NOTE: Program will only accept times between Jan 1 2015 through 2029 GMT");
  Serial.println("NOTE 2: Unix time is always in GMT, program will offset it using timezone_offset found in " + String(configFileName) + " on SD card");
}

// Pad the left size of a string with supplied character until it reaches padLength
String lPad (String stringToPad, char padChar, int padLength) {
  String lPadOutput = stringToPad;
  int i=0;
  while (lPadOutput.length() < padLength)
  {
    lPadOutput = String(padChar) + lPadOutput;
    i++;
    if (i > 1000) {
      logError("Not allowed to pad over 1000 characters.  String=" + stringToPad + " char=" + String(padChar) + " length=" + String(padLength));
      return "ERROR PADDING";
    }
  }
  return lPadOutput;
}

String zeroPad (int intToZeroPad, int padLength)
{
  return lPad(String(intToZeroPad, DEC), '0', padLength);
}

// Get timestamp in MM/DD/YYYY hh:mm:ss format
String getTimeStampString() {
  String timeStamp;
/*  DateTime time = rtc.now();
  timeStamp = zeroPad(time.month(),2) + "/" + zeroPad(time.day(),2) + "/" + zeroPad(time.year(),4) + " ";
  timeStamp += zeroPad(time.hour(),2) + ":" + zeroPad(time.minute(),2) + ":" + zeroPad(time.second(),2); */
  timeStamp = zeroPad(month(),2) + "/" + zeroPad(day(),2) + "/" + zeroPad(year(),4) + " ";
  timeStamp += zeroPad(hour(),2) + ":" + zeroPad(minute(),2) + ":" + zeroPad(second(),2); 
  return timeStamp;
}
////////////////////////End Time and logging Functions////////////////////////

////////////////////////Start High Tunnel Sensor Functions////////////////////////
boolean limitSwitchHit (String rollDirection, String rollSide, boolean logWhenLimitIsNotHit) {
  // TODO If these if statements are taking too long to run move them out to a function that only runs once per roll
  int limitSwitchId = west_winch_limit_pin;
  if (rollSide == "East")
    limitSwitchId = east_winch_limit_pin;
  
  int limitSwitchReading = analogRead(limitSwitchId);
  
  // Both top and bottom limit switches for each side are wired into a single analog pin using resistors
  // If the Top is hit alone it reads 346, if the Bottom is hit alone it reads 260 and if Both are hit it reads 172
  // Cushion the check by 20 bits in case the reads are slightly off
  int cushion = 20;
  int topHit = 346;
  int bottomHit = 260;
  int bothHit = 172;
  
  // Set dirHit based on the direction that you are checking
  int dirHit = bottomHit;
  if (rollDirection == "Up")
    dirHit = topHit;
  
  if (limitSwitchReading > dirHit - cushion && limitSwitchReading < dirHit + cushion) {
    logMessage(rollSide + " " + rollDirection + " limit switch hit! Analog Reading: " + String(limitSwitchReading));
    return true;
  }
  if (limitSwitchReading > bothHit - cushion && limitSwitchReading < bothHit + cushion) {
    logError ("Both limit switches on " + rollSide + " are hit! Something is wrong! Analog Reading:" + String(limitSwitchReading));
    return true;
  }

  if (logWhenLimitIsNotHit)
    logMessage (rollSide + " " + rollDirection + " limit switch NOT hit. Analog Reading:" + String(limitSwitchReading));
  return false;
}

// TODO: confirm that this code works with the actual sensors and their input values
float getTempFromSensor (String insideOrOutside, boolean disableManualEntry) {  
  float temperature=-9999.0;
  if (manual_sensor_entry_mode && !disableManualEntry) {
    Serial.println("manual_sensor_entry_mode is set to true so you must manually enter a temperature value");
    while (temperature == -9999.0) {
      Serial.println("Enter the " + insideOrOutside + " temperature in " + String(temperature_units) + " as a decimal value followed by a semicolon. exe. 73.2;");
      String inputValue = Serial.readStringUntil(';');
      if (confirmValidNum(inputValue, true, true, false))
        temperature = inputValue.toFloat();
    }
    logMessage("The " + insideOrOutside + " temperature sensor has a manually entered temperature of " + temperature + " " + String(temperature_units));
    return temperature;
  }

  // initialize to outside and reset to inside if needed
  float sensorRead;
  
  if (insideOrOutside == "inside")
    sensorRead = analogRead(inside_temp_sensor_pin);
  else 
    sensorRead = analogRead(outside_temp_sensor_pin);
  
  /* Temperature Conversion
     Divide the mV from the sensor by 100 to get temperature in C
     The sensor has 1023 bits for 5000 mV so 
     Temp in C = sensorRead/1023*5000/100 = sensorRead/1023*50 = sensorRead*50.0/1023.0
     For reference 60 F = (60-32)*5/9 = 15.5 C
       15.5 C = 15.5*100 = 1550 mV
       1550 mV = 1550/5000*1023 = 317 analogRead
     0 LSB = 0 C = 32 F
     Every 100 bits = 4.887 C = 8.797 F
  */
  temperature=sensorRead*50.0/1023.0;
  
  // Convert Celcius to Fahrenheit if set to F
  if (temperature_units == 'F')
      temperature = (temperature * 9.0)/ 5.0 + 32.0;
  logMessage("The " + insideOrOutside + " temperature sensor has a digital value of " + sensorRead + " which is " + temperature + " " + String(temperature_units));
  return temperature;
}

int checkSolarLevel () {
  // NOTE: solarReading is not currently used for anything other than logging so no need to allow manual entry of it
  int solarReading = analogRead(solar_sensor_pin);
  // TODO add code that converts this number into an easier to understand voltage
  logMessage("Solar panel output at " + String(solarReading));
  return solarReading;
}

// TODO confirm that High means person detected
boolean isPersonNearRollers() {
  int millisecondsWaited = 0;
  while (millisecondsWaited <= pir_wait_time_max_seconds*1000 && digitalRead(pir_sensor_pin) == HIGH) {
    delay (pir_milliseconds_between_checks);
    millisecondsWaited += pir_milliseconds_between_checks;
    logMessage("PIR Sensors have detected someone near the rollers! Will check again shortly");
  }

  // It doesn't make sense to manually set The PIR sensors value so we won't worry about manual_sensor_entry_mode for them
  if (digitalRead(pir_sensor_pin) == HIGH) {
    logMessage("PIR Sensors have detected someone near the rollers consistently, accepting that this will not change and moving on.");
    return true;
  }
  return false;
}

String getTunnelStatus () {
  // Check all sensors, solar level is currently just logged and not used for any logic
  checkSolarLevel();
  float insideTemp = getTempFromSensor ("inside", false);
  float outsideTemp = getTempFromSensor ("outside", false);
  float tempDelta = insideTemp - outsideTemp;
  
  checkForHeatWave(insideTemp);

  // check super cool first because it acts the samme whether artificial heat wave is enabled or not
  if (tempDelta < super_cool_delta)
    return "Super Cool";
    
  if (isArtificialHeatWaveEnabled()) {
    if (insideTemp <= heat_wave_enabled_warming_temperature)
      return "Too Cool";
    if (tempDelta > too_hot_delta && insideTemp > heat_wave_enabled_cooling_temperature)
      return "Too Hot";
    // If an artificial heat wave is enabled we never want to cool aggressively so never return Way Too Hot
    return "Just Right";
  }
  
  if (tempDelta > way_too_hot_delta)
    return "Way Too Hot";
  if (tempDelta > too_hot_delta)
    return "Too Hot";
  if (tempDelta < too_cool_delta)
    return "Too Cool";
  return "Just Right";
}

boolean isArtificialHeatWaveEnabled () {
  if (isHeatWaveSwitchOn()) {
    logMessage("Heatwave enabled");
    return true;
  }
  // TODO add code that reads and uses heatwave calendar entries from SD card
  return false;
}

boolean isHeatWaveSwitchOn () {
  // The heatwave switch is simple enough we won't worry about manual_sensor_entry_mode for it
  if (digitalRead(heat_wave_switch_pin) == HIGH) {
    logMessage("Heatwave switch set to on");
    return true;
  }
  return false;
}

void logHeatWaveDataAndResetVariables () {
  if (heatWaveHoursSurpassedToday) {
    logHeatWave("HEATWAVE HOURS: Over " + String(heat_wave_temperature) + " F continuously for " + String(heat_wave_hours_to_log) + " hours or more");
    heatWaveDaysInARow++;
    if (heatWaveDaysInARow >= heat_wave_days_to_log) {
      logHeatWave("HEATWAVE DAYS: The heatwave has continued for " + String(heatWaveDaysInARow) + " days");
      // TODO check to see if we should not reset heatWaveDaysInARow here and instead let it keep running up.
      // Exe. if it is hot for 6 days in a row does that count as 2 3 day heatwaves? If it is hot for 4 days what do we log?
      heatWaveDaysInARow = 0;
    }
  }
  else {
    heatWaveDaysInARow = 0;
    if (heatWaveTempSurpassedToday)
      logHeatWave("HEATWAVE MOMENT: It reached " + String(heat_wave_temperature) + " F for a moment today but not long enough to count as a heatwave day");
  }
  heatWaveHoursSurpassedToday = false;
  heatWaveTempSurpassedToday = false;
  heatWaveVarsResetDay = day();// rtc.now().day();
}

void checkForHeatWave (float insideTemp) {
  // Every time we reach a new day log the previous day's heat wave results and reset the heat wave variables
  if (day() != heatWaveVarsResetDay)//  if (rtc.now().day() != heatWaveVarsResetDay)
    logHeatWaveDataAndResetVariables();
  if (insideTemp > heat_wave_temperature) {
    heatWaveTempSurpassedToday = true;
    if (heatWaveRequiredEndTime == 0)
      heatWaveRequiredEndTime = hour()*60*60 + minute()*60 + second() + heat_wave_hours_to_log*SECONDS_IN_HOUR;
      // heatWaveRequiredEndTime = rtc.now().hour()*60*60 + rtc.now().minute()*60 + rtc.now().second() + heat_wave_hours_to_log*SECONDS_IN_HOUR;
    // else if (rtc.now().hour()*60*60 + rtc.now().minute()*60 + rtc.now().second() > heatWaveRequiredEndTime)
    else if (hour()*60*60 + minute()*60 + second() > heatWaveRequiredEndTime)
        heatWaveHoursSurpassedToday = true;
  }
  else {
    // If temp drops back down reset the heatWaveRequiredEndTime. TODO: Confirm that you should do this
    heatWaveRequiredEndTime = 0;
  }
}
////////////////////////End High Tunnel Sensor Functions////////////////////////


////////////////////////Start High Tunnel Control State Functions////////////////////////
void manageHighTunnelTemp () {
  String currentTunnelpStatus = getTunnelStatus();
  logMessage("Current Tunnel Status is " + currentTunnelpStatus);
  
  // if it is way too hot both roll up the sides and run the fans.
  if (currentTunnelpStatus=="Way Too Hot") {
    if (!shuttersOpen)
      changeShutters();
    rollSides("Up");
    runFans();
  }
  // if it is too hot open shutters then alternate between running fans and rolling up sides to try and cool down a bit only do one thing at a time to save energy
  else if (currentTunnelpStatus=="Too Hot") {
    if (!shuttersOpen)
      changeShutters();
    // If both of the sides have been rolled completely then just continue with fans
    else if (runFansNext || (limitSwitchHit("Up", "East", false) && limitSwitchHit("Up", "West", false)))
      runFans();
    else
      rollSides("Up");
  }
  // if it is colder inside than out then run the fans a bit to bring in the warmer outside air
  else if (currentTunnelpStatus=="Super Cool") {
    if (!shuttersOpen)
      changeShutters();
    runFans();
  }
  // if it is too cool inside close the shutters, if that does not work then try rolling down the sides a bit
  else if (currentTunnelpStatus=="Too Cool") {
    if (shuttersOpen)
      changeShutters();
    else
      rollSides("Down");
  }
  else if (currentTunnelpStatus=="Just Right")
    logMessage("Temp is in acceptable bounds, leaving everything as is");
  else
    logError("Unexpected value " + currentTunnelpStatus + " returned by getTunnelStatus().");
}

// Note: LOW is open direction, high is closed
void changeShutters () {
  if (!shuttersOpen) {
    logMessage("Opening closed shutters");
    digitalWrite(north_shutters_direction_digital_pin, LOW);
    digitalWrite(south_shutters_direction_digital_pin, LOW);
    shuttersOpen = true;
  }
  else {
    logMessage("Closing open shutters");
    digitalWrite(north_shutters_direction_digital_pin, HIGH);
    digitalWrite(south_shutters_direction_digital_pin, HIGH);
    shuttersOpen = false;
  }
  // Delay for a moment to ensure shutter direction relay is powered up
  delay (100);
  digitalWrite(north_shutters_power_digital_pin, HIGH);
  digitalWrite(south_shutters_power_digital_pin, HIGH);
  // Delay for another moment to give shutter time to open
  delay(200);
  
  // Power down both relays
  digitalWrite(north_shutters_direction_digital_pin, LOW);
  digitalWrite(south_shutters_direction_digital_pin, LOW);
  digitalWrite(north_shutters_power_digital_pin, LOW);
  digitalWrite(south_shutters_power_digital_pin, LOW);
}

void rollSides (String rollDirection) {
  rollSide(rollDirection, "East");
  rollSide(rollDirection, "West");
  // Only run fans next if they are set to run for some time. If they are set to run 0 seconds they are disabled
  if (fan_run_seconds > 0)
    runFansNext = true;
}

void rollSide (String rollDirection, String rollSide) {
  setRollDirection(rollDirection, true, rollSide);
  int millisecondsRolling = 0;
  int rollPowerPin;
  
  if (rollSide == "East") 
    rollPowerPin = east_winch_roll_power_digital_pin;
  else 
    rollPowerPin = west_winch_roll_power_digital_pin;
  
  logMessage("Starting to roll " + rollSide + " side " + rollDirection);
    
  if (!limitSwitchHit(rollDirection, rollSide, false) && !isPersonNearRollers()) {
    digitalWrite(rollPowerPin, HIGH);
  }
  
  while (millisecondsRolling <= winch_roll_seconds*1000 && !limitSwitchHit(rollDirection, rollSide, false) && !isPersonNearRollers()) {
    delay (winch_roll_milliseconds_between_limit_check);
    millisecondsRolling += winch_roll_milliseconds_between_limit_check;
  }
  
  digitalWrite(rollPowerPin, LOW);
  logMessage("Ending " + rollSide + " side roll " + rollDirection + " after " + String(millisecondsRolling) + " ms.");
  setRollDirection(rollDirection, false, rollSide);
}

// Note: Down is LOW Up is HIGH
void setRollDirection(String rollDirection, boolean powerOn, String rollSide)
{
  int rollWinchPin;
  if (rollSide == "East")
    rollWinchPin = east_winch_roll_direction_digital_pin;
  else
    rollWinchPin = west_winch_roll_direction_digital_pin;
    
  if(powerOn) {
    logMessage("Setting roll direction for " + rollSide + " side to " + rollDirection);
    if (rollDirection == "Down")
      digitalWrite(rollWinchPin, LOW);
    else 
      digitalWrite(rollWinchPin, HIGH);
    delay (100); // Add a short delay to ensure the relay has time to change states TODO confirm this delay time is good
    
  }
  else {
    logMessage("Rolling complete, powering down " + rollSide + " roll direction relays");
    digitalWrite(rollWinchPin, LOW);
  }
}

void runFans() {
  logMessage ("Running fans for " + String(fan_run_seconds) + " seconds");
  digitalWrite(fan_power_digital_pin, HIGH);
  delay(fan_run_seconds*1000);
  digitalWrite(fan_power_digital_pin, LOW);
  // Only run winches next if they are set to run for some time. If they are set to run 0 seconds they are disabled
  if (winch_roll_seconds > 0)
    runFansNext = false;
}
////////////////////////End High Tunnel Control State Functions////////////////////////

// Configure digital output pins as such
void setDigitalPinModes () {
  logMessage("Configuring Digital Pin Modes");
  setPinToOutput(east_winch_roll_direction_digital_pin);
  setPinToOutput(east_winch_roll_power_digital_pin);
  setPinToOutput(west_winch_roll_direction_digital_pin);
  setPinToOutput(west_winch_roll_power_digital_pin);
  setPinToOutput(north_shutters_direction_digital_pin);
  setPinToOutput(south_shutters_direction_digital_pin);
  setPinToOutput(north_shutters_power_digital_pin);
  setPinToOutput(south_shutters_power_digital_pin);
  setPinToOutput(fan_power_digital_pin);
  setPinToInput(heat_wave_switch_pin);
  setPinToInput(pir_sensor_pin);
}

void setPinToOutput (int pinID) {
  logMessage("Setting pinID: " + String(pinID) + " to Output");
  pinMode(pinID, OUTPUT);
}

void setPinToInput (int pinID) {
  logMessage("Setting pinID: " + String(pinID) + " to Input");
  pinMode(pinID, INPUT);
}

void setup(){
  Serial.begin(9600);
  wf.begin();
/*  if (! rtc.begin())
    Serial.println("ERROR: Couldn't find RTC (DS1307 Real Time Clock)");

  if (! rtc.isrunning()) {
    Serial.println("RTC is NOT running!");
    // following line sets the RTC to the date & time this sketch was compiled
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    // This line sets the RTC with an explicit date & time, for example to set
    // January 21, 2014 at 3am you would call:
    // rtc.adjust(DateTime(2014, 1, 21, 3, 0, 0));
  }
  delay(500);*/

  setupSdCard();
  ensureLogFileExists(logFileName);
  ensureLogFileExists(heatWaveFileName);
  ensureLogFileExists(configFileName);
  readConfigFromFile();
  Serial.println();
  setDigitalPinModes();
  printHelp();
}

void loop() {
  char incomingByte;
  if (automatically_start_high_tunnel_control)
    processSerialInput('b');
    
  if (Serial.available() > 0){
    delay(100);
    incomingByte = Serial.read();
    Serial.print("     Command received: ");
    Serial.println(incomingByte);
    processSerialInput(incomingByte);
  }
}
