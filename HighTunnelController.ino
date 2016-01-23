#include <Wire.h>
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

// Variables that should be passed in via config file
int gmt_hour_offset = -5; // -5 is the EST offset
float too_hot_delta = 6.0;  // If difference between inside and outside temp is more than this start to cool
float way_too_hot_delta = 7.5; // If difference between inside and outside temp is more than this use more aggressive cooling measures
float too_cool_delta = 4.0; // If difference between inside and outside temp is less than this start to warm
float super_cool_delta = -1; // If difference between inside and outside temp is less than this and warm up inside using outside air
boolean disable_log_file = false; // setting this to true disables writing to log file. Can help debug memory card issues
boolean manual_delta_temp_entry_mode = true; //If true then read delta temps from Serial input by user instead of actual sensors.  Used to test system.
char temperature_units = 'F'; //Set ot F for Farenheight of C for Celcius.  If you change this you should probably change the deltas too.

// Variables that are internal to the program
boolean sdCardWorking = false;
boolean logFileWorking = false;
String lastAction = "nothing"; //State ariable used by program to help decide next action can be nothing, fans, or sides
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
      processKeyValuePair(key,value);
    }
    // close the file:
    configFile.close();
  } else 
    logMessage("Error opening config file " + String(configFileName));
}

void processKeyValuePair(String key, String value) {
  logMessage("Processing key value pair of key=" + key + " value=" + value);
  if (key == "gmt_hour_offset" && confirmValidNum(value,true,false)) {
    gmt_hour_offset = value.toInt();
    logMessage("Config File setting gmt_hour_offset to " + String(gmt_hour_offset));
  }
  else {
    logMessage("key:'" + key + "' is not valid.  Ignoring value:'" + value + "'");
  }
}

boolean confirmValidNum(String str, boolean allowNegative, boolean allowDecimalPoint) {
  boolean isValidNum=false;
  boolean oneDecimalFound=false;
  int i = 0;
  // if we want to allow negatives and the first char is a minus sign ignore it
  if (allowNegative && str.charAt(0)=='-')
    i=1;
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
  Serial.println("l; Read all data from log file");
  Serial.println("cY; Remove all data from log file");
  Serial.println("f; Print all the files and folders in base dir");
  Serial.println("TValue; Set internal time to Value (given as unix timestamp)");
  Serial.println("t; Print currently set date/time to Serial");
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
  // offset time using gmt_hour_offset;
  if(parsedTime > MIN_ALLOWABLE_DATE_TIME && parsedTime < MAX_ALLOWABLE_DATE_TIME) {
    logMessage("Updating time using the configured gmt_hour_offset of " + String(gmt_hour_offset));
    parsedTime = parsedTime + gmt_hour_offset * SECONDS_IN_HOUR;
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
  timeStamp = zeroPad(month(),2) + "/" + zeroPad(day(),2) + "/" + zeroPad(year(),4) + " ";
  timeStamp += zeroPad(hour(),2) + ":" + zeroPad(minute(),2) + ":" + zeroPad(second(),2);
  return timeStamp;
}
//////////////////////// End Time and logging Functions ////////////////////////

////////////////////////Start High Tunnel Sensor Functions////////////////////////
float getCurrentTempDelta() {
  float deltaTemp=-9999.0;
  if (manual_delta_temp_entry_mode) {
    Serial.println("manual_delta_temp_entry_mode is set to true so you must manually enter a temperature value");
    Serial.println("Enter inside temp - outside temp (in " + String(temperature_units) + ") as a decimal value followed by a semicolon. eve. 3.2;");
    while (deltaTemp == -9999.0) {
      String inputValue = Serial.readStringUntil(';');
      if (confirmValidNum(inputValue, true, true))
        deltaTemp = inputValue.toFloat();
    }
  }
  else {
    // TODO: Add code here to pull temperature sensor values from inside and outside, convert from volts to temp in temperature_units and return difference)
    if (temperature_units = 'F')
      deltaTemp = 4.0;
    else {
      // If temperature_units is not F assume C
      deltaTemp = 3.0;
    }
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

/*
boolean runFansNext = true; //Used to alternate between running fans and rolling sides
boolean shuttersOpen = false; //Used to track shutter status
*/

////////////////////////Start High Tunnel Control State Functions////////////////////////
void manageHighTunnelTemp () {
  String currentTempStatus = getTemperatureStatus();
  logMessage("Current Temperature Status is" + currentTempStatus);
  
  if (currentTempStatus=="Way Too Hot") {
    logMessage("It's way too hot relative to outside so opening sides a bit and running fans a bit");
    rollUpSides();
    runFans();
  }
  else if (currentTempStatus=="Too Hot") {
    logMessage("It's a bit too hot relative to outside");
    if (!shuttersOpen)
      openShutters();
    else if (runFansNext)
      runFans();
    else
      rollUpSides();
  }
  else if (currentTempStatus=="Super Cool") {
  }
  else if (currentTempStatus=="Too Cool") {
  }
  else if (currentTempStatus=="Just Right") {
  }
  else {
    logError("Unexpected value returned by getTemperatureStatus().");
  }
}

void rollUpSides () {
}

void runFans() {
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
