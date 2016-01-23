#include <Time.h>
#include <SD.h>
#include <WildFire.h>

// Constants used by program, note that the file names are essentially constants but specifying that causes an error
const unsigned long MIN_ALLOWABLE_DATE_TIME = 1420156799; // 1 second before Jan 1 2015 GMT
const unsigned long MAX_ALLOWABLE_DATE_TIME = 1893520800; // Jan 1 2030 GMT
const unsigned long UNSET_TIME_DEFAULT      = 915148800L;  //Jan 1 1999 GMT
const unsigned long SECONDS_IN_HOUR         = 3600;
char configFileName[] = "live.cfg"; //recommended to leave this set and rename a different file as live.cfg to update it
char logFileName[] = "live.log"; //recommended to leave this set.  Multiple log files will be spun off as we go.

// External variables that can be passed in via config file
int gmt_hour_offset = -5; // -5 is the EST offset
float too_hot_delta = 6.0;  // If difference between inside and outside temp is more than this start to cool
float way_too_hot_delta = 7.5; // If difference between inside and outside temp is more than this use more aggressive cooling measures
float too_cool_delta = 4.0; // If difference between inside and outside temp is less than this start to warm
float super_cool_delta = -1.0; // If difference between inside and outside temp is less than this and warm up inside using outside air
boolean disable_log_file = false; // setting this to true disables writing to log file. Can help debug memory card issues
boolean manual_delta_temp_entry_mode = true; //If true then read delta temps from Serial input by user instead of actual sensors.  Used to test system.
char temperature_units = 'F'; //Set ot F for Farenheight of C for Celcius.  If you change this you should probably change the deltas too.
int inside_temp_sensor_pin = 0;
int outside_temp_sensor_pin = 1;
float thermistor_B = 1.0; // Thermistor B parameter - found in datasheet 
float thermistor_T0 = 1.0; // Manufacturer T0 parameter - found in datasheet (kelvin)
float thermistor_R0 = 1.0; // Manufacturer R0 parameter - found in datasheet (ohms)
float thermistor_R_Balance = 1.0; // Your balance resistor resistance in ohms
unsigned long millisecond_delay_between_actions = 10000; // How long to wait between reading temperature and taking action to correct it

// Internal variables that the program manages itself
boolean sdCardWorking = false;
boolean logFileWorking = false;
boolean runFansNext = true; //Used to alternate between running fans and rolling sides
boolean shuttersOpen = false; //Used to track shutter status
WildFire wf;

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

void ensureLogFileExists () {
  Serial.println("Ensuring log file exists on SD Card.  Filename=" + String(logFileName));
  File myFile = SD.open(logFileName, FILE_READ);
  if (myFile) {
    myFile.close();
    logMessage("Log File found");
  }
  else {
    logFileWorking = false;
    logMessage("Log File not found");
    writeSdFile("Initializing Log File", logFileName);
  }
  myFile = SD.open(logFileName, FILE_READ);
  if (myFile)
    logFileWorking = true;
  else {
    logFileWorking = false;
    logError("Problem creating log file");
  }
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
  else if (validKey(key, "manual_delta_temp_entry_mode", printOutMode) && confirmValidBool(value, false, printOutMode))
    manual_delta_temp_entry_mode = processConfigBool(key,value);
  else if (validKey(key, "temperature_units", printOutMode) && confirmValidTemperatureUnit(value, printOutMode))
    temperature_units = value.charAt(0);
  else if (validKey(key, "thermistor_B", printOutMode) && confirmValidNum(value,false,true, printOutMode))
    thermistor_B = processConfigFloat(key,value);
  else if (validKey(key, "thermistor_T0", printOutMode) && confirmValidNum(value,false,true, printOutMode))
    thermistor_T0 = processConfigFloat(key,value);
  else if (validKey(key, "thermistor_R0", printOutMode) && confirmValidNum(value,false,true, printOutMode))
    thermistor_R0 = processConfigFloat(key,value);
  else if (validKey(key, "thermistor_R_Balance", printOutMode) && confirmValidNum(value,false,true, printOutMode))
    thermistor_R_Balance = processConfigFloat(key,value);
  else if (validKey(key, "millisecond_delay_between_actions", printOutMode) && confirmValidNum(value,false,false,printOutMode))
    millisecond_delay_between_actions = processConfigInt(key,value);
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
  myFile = SD.open(sdFileName, FILE_WRITE);
  if (myFile) {
    myFile.println(sdMessage);
    myFile.close();
  }
  else 
    Serial.println("WRITE ERROR: Issue opening file for writing!");
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

void printHelp() {
  Serial.println("Acceptable commands via the Serial Monitor");
  Serial.println("If you omit the ; it will wait a second before processing");
  Serial.println("wValue; Write Value to a new line in the config file");
  Serial.println("r; Read all lines from config file");
  Serial.println("eY; Remove all data from config file");
  Serial.println("s; Set key value pairs from config file");
  Serial.println("p; Print all allowed key value pairs and allowable inputs for each");
  Serial.println("l; Read all data from log file");
  Serial.println("cY; Remove all data from log file");
  Serial.println("f; Print all the files and folders in base dir");
  Serial.println("TValue; Set internal time to Value (given as unix timestamp)");
  Serial.println("t; Print currently set date/time to Serial");
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
    case 'b':
      Serial.println("Running high tunnel control.");
      char cancelByte;
      while (cancelByte != 'x') {
        manageHighTunnelTemp();
        Serial.println("Enter x to quit out of it and return to serial interface");
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
    logMessage("Updating time.  Current gmt offset is " + String(gmt_hour_offset));
    setTime(parsedTime);
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
  Serial.println("Get current unix timestamp (also called Epoch time, meaning seconds since Jan 1 1970)");
  Serial.println("On a mac or unix system simply type date +%s into the terminal");
  Serial.println("http://www.epochconverter.com/ also has it");
  Serial.println("Then enter it into Serial Monitor prefaced by a T and followed by a ;");
  Serial.println("Example: T1420156799; is 1 second before Jan 1 2015 GMT");
  Serial.println("NOTE: Program will only accept times between Jan 1 2015 through 2029 GMT");
  Serial.println("NOTE 2: Unix time is always in GMT, program will offset it using timezone_offset found in " + String(configFileName) + " on SD card");
  Serial.println("NOTE 3: If you do not set clock it will start at Jan 1 1999 so the log can track relative times");
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
  if (timeStatus() == timeNotSet) {
    logError("Time status is timeNotSet, this should never be");
    return "ERROR: UNSET TIME";
  }
  // offset time using gmt_hour_offset
  adjustTime(gmt_hour_offset * SECONDS_IN_HOUR);
  timeStamp = zeroPad(month(),2) + "/" + zeroPad(day(),2) + "/" + zeroPad(year(),4) + " ";
  timeStamp += zeroPad(hour(),2) + ":" + zeroPad(minute(),2) + ":" + zeroPad(second(),2);
  // return time to non-offset mode to keep it a true timezone free Epoch
  // If this leads to the time getting off faster we can remove it and just set
  // epoch with the offset once and leave it, but that means that if you ever output
  // an epoch timestamp it won't be set to GMT as it should be.
  adjustTime(gmt_hour_offset * SECONDS_IN_HOUR * -1);
  return timeStamp;
}
//////////////////////// End Time and logging Functions ////////////////////////

////////////////////////Start High Tunnel Sensor Functions////////////////////////
// TODO: confirm that this code works with the actual sensors and their input values
float getTempFromSensor (String insideOrOutside) {
  float sensorRead;
  if (insideOrOutside == "inside") 
    sensorRead = analogRead(inside_temp_sensor_pin);
  else 
    sensorRead = analogRead(outside_temp_sensor_pin);
  
  // Temperature conversion code adapted from http://playground.arduino.cc/ComponentLib/Thermistor2
  float thermistor_R=thermistor_R_Balance*(1024.0f/sensorRead-1);
  float temperature=1.0f/(1.0f/thermistor_T0+(1.0f/thermistor_B)*log(thermistor_R/thermistor_R0)) - 273.15;
  // Convert Celcius to Fahrenheit if set to F
  if (temperature_units == 'F')
      temperature = (temperature * 9.0)/ 5.0 + 32.0;
  logMessage("The " + insideOrOutside + " temperature sensor has a digital value of " + sensorRead + " which is " + temperature + " " + String(temperature_units));
  return temperature;
}

float getCurrentTempDelta() {
  float deltaTemp=-9999.0;
  if (manual_delta_temp_entry_mode) {
    Serial.println("manual_delta_temp_entry_mode is set to true so you must manually enter a temperature value");
    Serial.println("Enter inside temp - outside temp (in " + String(temperature_units) + ") as a decimal value followed by a semicolon. eve. 3.2;");
    while (deltaTemp == -9999.0) {
      String inputValue = Serial.readStringUntil(';');
      if (confirmValidNum(inputValue, true, true, false))
        deltaTemp = inputValue.toFloat();
    }
  }
  else {
    float insideTemp = getTempFromSensor ("inside");
    float outsideTemp = getTempFromSensor ("outside");
    deltaTemp = insideTemp - outsideTemp;
    logMessage ("Temp Reading in " + String(temperature_units) + ". Inside=" + String(insideTemp)  + " Outside=" + String(outsideTemp) + " Diff=" + deltaTemp);
  }
  return deltaTemp;
}

String getTemperatureStatus () {
  float tempDelta = getCurrentTempDelta();
  if (tempDelta > way_too_hot_delta)
    return "Way Too Hot";
  if (tempDelta > too_hot_delta)
    return "Too Hot";
  if (tempDelta < super_cool_delta)
    return "Super Cool";
  if (tempDelta < too_cool_delta)
    return "Too Cool";
  return "Just Right";
}
////////////////////////Start High Tunnel Sensor Functions////////////////////////


////////////////////////Start High Tunnel Control State Functions////////////////////////
void manageHighTunnelTemp () {
  String currentTempStatus = getTemperatureStatus();
  logMessage("Current Temperature Status is " + currentTempStatus);
  
  // if it is way too hot both roll up the sides and run the fans.
  if (currentTempStatus=="Way Too Hot") {
    openShuttersIfNeeded();
    rollSides("Up");
    runFans();
  }
  // if it is too hot open shutters then alternate between running fans and rolling up sides to try and cool down a bit only do one thing at a time to save energy
  else if (currentTempStatus=="Too Hot") {
    if (!shuttersOpen)
      changeShutters();
    else if (runFansNext)
      runFans();
    else
      rollSides("Up");
  }
  // if it is colder inside than out then run the fans a bit to bring in the warmer outside air
  else if (currentTempStatus=="Super Cool") {
    openShuttersIfNeeded();
    runFans();
  }
  // if it is too cool inside close the shutters, if that does not work then try rolling down the sides a bit
  else if (currentTempStatus=="Too Cool") {
    if (shuttersOpen)
      changeShutters();
    else
      rollSides("Down");
  }
  else if (currentTempStatus=="Just Right") {
    logMessage("Temp is in acceptable bounds, leaving everything as is");
  }
  else {
    logError("Unexpected value returned by getTemperatureStatus().");
  }
}

void openShuttersIfNeeded () {
  if (!shuttersOpen) {
    changeShutters();
  }
}

void changeShutters () {
  if (shuttersOpen) {
    logMessage("Opening shutters");
    // TODO add code to actually open shutters
    shuttersOpen = true;
  }
  else {
    logMessage("Closing shutters");
    // TODO add code to actually close shutters
    shuttersOpen = false;
  }  
}

void rollSides (String rollDirection) {
  if (rollDirection = "Up") {
    logMessage ("Rolling sides up a bit");
  }
  else {
    logMessage ("Rolling sides down a bit");
  }
  runFansNext = true;
  // TODO add code to actually roll the sides
  // set direction for a while before you roll so rolling alarm horn will sound
  // ensure that we have not hit the limit sensors while rolling
}

void runFans() {
  // TODO add code to actually run the fans
  logMessage ("Running fans");
  runFansNext = false;
}
//////////////////////// End High Tunnel Control State Functions ////////////////////////

void setup(){
  Serial.begin(9600);
  wf.begin();
  delay(1000);

  setupSdCard();
  setTime(UNSET_TIME_DEFAULT);
  ensureLogFileExists();
  logMessage("Time initialized at startup.  Reset Manaually ASAP!");

  readConfigFromFile();
  timeHelp();
  Serial.println();
  printHelp();
}

void loop(){
  char incomingByte;
  if (Serial.available() > 0){
    incomingByte = Serial.read();
    Serial.print("     Command received: ");
    Serial.println(incomingByte);
    processSerialInput(incomingByte);
  }
}
