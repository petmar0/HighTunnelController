/*
Pin Definitions
Analog
A0 West limit switches
A1 Outside Temp
A2 Inside Temp
A3 East limit switches
A4 
A5 
A6 PIR
A7 Solar (not currently connected)

Digital
D2 - Fans
D3 - East Winch Direction (HIGH is down)
D4 - East Winch Power (LOW sends power)
D5 - West Winch Power (LOW sends power)
D6 - Onboard LED
D7 - South Actuator Direction (HIGH is open)
D8 - South Actuator Power (LOW sends power)
D9 - North Actuator Direction (HIGH is open)
D10 - North Actuator Power (LOW sends power)
D11 - West Winch Direction (HIGH is down)
D12 - Heat Wave Switch

Notes, the W Side dir relay flicks on and off a bunch when uploading code or 
starting serial monitor. It is tied to D5 so maybe serial touches that pin?
*/
