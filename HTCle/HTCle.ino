float threshold=6;
int dly=1;
float k1=0.217;
float k2=4;

void setup() {
  // put your setup code here, to run once:
  Serial.begin(9600);
  pinMode(2, OUTPUT);
}

void loop() {
  // put your main code here, to run repeatedly:
  Serial.print(millis());
  Serial.print(",");
  int Ti=analogRead(A2);
  int To=analogRead(A1);
  Serial.print(k1*Ti+k2);
  Serial.print(",");
  Serial.print(k1*To+k2);
  Serial.print(",");
  if((k1*Ti+k2)<=threshold+(k1*To+k2)||(k1*Ti+k2)<95){
    digitalWrite(2, HIGH);
    Serial.println("Fans off.");
  }
  else if((k1*Ti+k2)>threshold+(k1*To+k2)){
    digitalWrite(2, LOW);
    Serial.println("Fans on.");
  }
  delay(dly*1000);
}
