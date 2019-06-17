# radmon-v2

![alt text](https://images-na.ssl-images-amazon.com/images/I/61fP35TuMKL._SL1000_.jpg)

ESP32 Geiger Counter with SD support
-----------------------------
Designed to be lower power avoiding continuous WiFI operation. WiFI only enabled when transmitting results online. 

Geiger counter readings stored to SD card and then pulled at a pretertermined interval to be trasmitted to ThingsLabs for analysis. 

Alerts can be configured if radiation level exceeds a configured threshold. Sent via SMS using the inbuilt IFTTT API call.

Hardware details
----------------------------
Standard ESP32 module
Tempeture senstor DS18S20
Geiger tube M4011
SD Reader

Geiger Counter Kit
https://www.banggood.com/Assembled-DIY-Geiger-Counter-Kit-Module-Miller-Tube-GM-Tube-Nuclear-Radiation-Detector-p-1136883.html

SD Reader
https://www.amazon.com/SunFounder-Module-Socket-Reader-Arduino/dp/B01G8BQV7A
