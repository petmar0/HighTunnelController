// Pin Tester, lets you test pins via serial interface
#include <WildFire.h>

WildFire wf;

void setup(){
  Serial.begin(9600);
  wf.begin();
  Serial.println("Pin Tester, lets you test pins via serial interface");
  Serial.println("Here are some examples, remember to include the letter, number and semi-colon!");
  Serial.println("h2; would set digital pin 2 to high");
  Serial.println("l3; would set digital pin 3 to low");
  Serial.println("d4; would read digital pin 4");
  Serial.println("a5; would read analog pin 5 (note analog pins must be between 0 and 7)");
}

void processSerialInput (char serialCommand){
  String inputValue = Serial.readStringUntil(';');
  int inputPin = inputValue.toInt();
  Serial.println(" for pin " + inputValue);
  int pinReading = -1;
  
  switch (serialCommand) {
    case 'l':
      pinMode(inputPin, OUTPUT);
      digitalWrite(inputPin, LOW);
      Serial.println("Setting pin " + String(inputPin) + " to output LOW");
      break;
    case 'h':
      pinMode(inputPin, OUTPUT);
      digitalWrite(inputPin, HIGH);
      Serial.println("Setting pin " + String(inputPin) + " to output HIGH");
      break;
    case 'd':
      pinMode(inputPin, INPUT);
      pinReading = digitalRead(inputPin);
      Serial.println("Read pin " + String(inputPin) + " and got: " + String(pinReading));
      break;
    case 'a':
      if (inputPin == 0)
        pinReading = analogRead(A0);
      if (inputPin == 1)
        pinReading = analogRead(A1);
      if (inputPin == 2)
        pinReading = analogRead(A2);
      if (inputPin == 3)
        pinReading = analogRead(A3);
      if (inputPin == 4)
        pinReading = analogRead(A4);
      if (inputPin == 5)
        pinReading = analogRead(A5);
      if (inputPin == 6)
        pinReading = analogRead(A6);
      if (inputPin == 7)
        pinReading = analogRead(A7);
      Serial.println("Read pin " + String(inputPin) + " and got: " + String(pinReading));
      break;
  }
}

void loop() {
  char incomingByte;

  if (Serial.available() > 0){
    delay(100);
    incomingByte = Serial.read();
    Serial.print("     Command received: ");
    Serial.print(incomingByte);
    processSerialInput(incomingByte);
  }
}

