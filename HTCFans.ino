#include "math.h"                           //Get the math library for math functions
#include "SparkFunPhant/SparkFunPhant.h"    //Get the Phant library for Phant functions

const char server[] = "data.sparkfun.com"; // Phant destination server
const char publicKey[] = "YGoKdGpEZ2TOJ4Wzw5nW"; // Phant public key
const char privateKey[] = "RbdJBbEVNlCXEpbqN64b"; // Phant private key
Phant phant(server, publicKey, privateKey); // Create a Phant object
unsigned long lastPost = 0; // global variable to keep track of last post time

int FanPin=2;    //Pin that the fan relays are wired to
int ToutPin=A0;   //Pin that the outside thermistor is wired to
int TinPin=A1;    //Pin that the inside thermistor is wired to
int SolarPin=A5;  //Pin that the solar panel voltage divider is wired to
int TboxPin=A6;   //Pin that the control box thermistor is wired to
int HWSwitch=7;   //Pin that the heat wave switch is wired to

int dly=10;     //Hysteresis delay for all functions (sec)
float thresh=5.5; //Temperature delta threshold setpoint float (°F)
float HWThresh=96;  //Heat wave threshold setpoint float (°F)
float k1=0.054; //Slope constant for thermistors (°F/LSB)
float k2=10;          //Offset constant for thermistors (°F)
int POST_RATE=1000*dly; //The post rate in msec is equal to 1000 times the dly in seconds

boolean HW=FALSE; //Heat wave storage boolean
float Tout=0;   //Outside temperature storage float
float Tin=0;    //Inside temperature storage float
float Tbox=0;   //Control box temperature storage float
float deltaT=0;   //Temperature difference storage float
float Solar=0;    //Solar panel output storage float

void setup(){
  Particle.publish("ROBOT ENERGIZED");    //Update status on Particle dashboard
  Time.zone(-4);                            //Set the time zone as EDT
  pinMode(ToutPin, INPUT);                //Set the outside thermistor pin as an input
  pinMode(TinPin, INPUT);                 //Set the inside thermistor pin as an input
  pinMode(SolarPin, INPUT);               //Set the solar panel voltage divider pin as an input
  pinMode(TboxPin, INPUT);                //Set the control box thermistor pin as an input
  pinMode(HWSwitch, INPUT);               //Set the heat wave switch pin as an input
  digitalWrite(FanPin, HIGH);             //Put the fan relays to off state
}

void loop(){
  Particle.publish("Time", Time.timeStr()); //Send the current time to the Particle event log online
  digitalWrite(FanPin, HIGH);             //Put the fan relays to off state
  Tout=k1*analogRead(ToutPin)+k2;         //Get the outside temperature by converting ToutPin LSB to °F
  Tin=k1*analogRead(TinPin)+k2;           //Get the inside temperature by converting TinPin LSB to °F
  Tbox=k1*analogRead(TboxPin)+k2;         //Get the control box temperature by converting TboxPin LSB to °F
  deltaT=Tin-Tout;                        //Get the temperature delta by subtracting outside from inside
  Solar=33*analogRead(SolarPin)/4096;     //Get the panel voltage from the 10:1 divider by converting LSB to VDC
  HW=digitalRead(HWSwitch);               //Get the state of the heat wave switch
  if(thresh<deltaT && !HW && Time.hour()<19 && Time.hour()>6){               //If the threshold is less than the delta, and HW is FALSE
    digitalWrite(FanPin, LOW);          //Turn on the fans
    Particle.publish("Fans","On");      //Tell the Particle event log that the fans are on
  }
  if(HW==TRUE && Tin>HWThresh && Time.hour()<19 && Time.hour()>6){      //Otherwise if HW is TRUE, it's between 6a and 7p, and the inside temperature is greater than the heat wave threshold
    digitalWrite(FanPin, LOW);          //Turn on the fans
    Particle.publish("Fans","On (HW)"); //Tell the Particle event log that the fans are on in HW mode
  }
  if(HW==TRUE && Tin<=HWThresh){        //If HW is TRUE and the inside temperature is below the HW threshold
    digitalWrite(FanPin, HIGH);       //Turn off the fans
    Particle.publish("Fans","Off (HW)");  //Tell the Particle event log that the fans are off and that HW is TRUE
  }
  postToPhant(Tout,Tin,deltaT,Tbox,Solar,0,0,0,HW);   //Post all values to https://data.sparkfun.com/streams/YGoKdGpEZ2TOJ4Wzw5nW
  delay(dly*1000);                       //Delay by the dly amount
  digitalWrite(FanPin, HIGH);            //Turn off the fans
}

int postToPhant(float tout,float tin,float deltat,float tbox,float solar,int pir,int elim,int wlim,bool hw)
{
  // Use phant.add(<field>, <value>) to add data to each field.
  // Phant requires you to update each and every field before posting,
  // make sure all fields defined in the stream are added here.
  phant.add("tout", tout);
  phant.add("tin", tin);
  phant.add("deltat", deltat);
  phant.add("tbox", tbox);
  phant.add("solar", solar);
  phant.add("pir", pir);
  phant.add("elim", elim);
  phant.add("wlim", wlim);
  phant.add("hw", hw);

  TCPClient client;
  char response[512];
  int i = 0;
  int retVal = 0;

  if (client.connect(server, 80)) // Connect to the server
  {
    // Post message to indicate connect success
    Serial.println("Posting!");

    // phant.post() will return a string formatted as an HTTP POST.
    // It'll include all of the field/data values we added before.
    // Use client.print() to send that string to the server.
    client.print(phant.post());
    delay(1000);
    // Now we'll do some simple checking to see what (if any) response
    // the server gives us.
    while (client.available())
    {
      char c = client.read();
      Serial.print(c);  // Print the response for debugging help.
      if (i < 512)
        response[i++] = c; // Add character to response string
      }
      // Search the response string for "200 OK", if that's found the post
      // succeeded.
      if (strstr(response, "200 OK"))
      {
        Serial.println("Post success!");
        retVal = 1;
      }
      else if (strstr(response, "400 Bad Request"))
    { // "400 Bad Request" means the Phant POST was formatted incorrectly.
    // This most commonly ocurrs because a field is either missing,
    // duplicated, or misspelled.
    Serial.println("Bad request");
    retVal = -1;
  }
  else
  {
    // Otherwise we got a response we weren't looking for.
    retVal = -2;
  }
}
else
{ // If the connection failed, print a message:
  Serial.println("connection failed");
  retVal = -3;
}
client.stop();  // Close the connection to server.
return retVal;  // Return error (or success) code.
}
