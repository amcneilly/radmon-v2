/*

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
*/

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <OneWire.h>
#include <IFTTTMaker.h>
#include "time.h"

#include "FS.h"
#include "SD.h"
#include "SPI.h"

//file on SD card
const char *logFile = "/data.log";
int debugData = 0;

#ifdef __cplusplus
extern "C" {
#endif

uint8_t temprature_sens_read();
//uint8_t g_phyFuns;

#ifdef __cplusplus
}
#endif

//based on number of Giger counter readings 
//default Gieger reading every 1 minute LOG_PERIOD. so if this is 120 go online every 2 hours to send data
#define ReadingsTransmitThreshold 120

//Used for CPM (counts per minute) calculation from the miller tube
//Logging period in milliseconds
#define LOG_PERIOD 60000 
//1 minute value in milliseconds
#define MINUTE_PERIOD 60000

/* DS18S20 Temperature chip i/o */
OneWire  ds(27);  // on pin 27

#ifndef CREDENTIALS
// WLAN
#define ssid  "SSID"
#define password "PASSWORD"

// IFTTT KEY used for SMS ALERT
#define IFTTT_KEY "IFTTTKEY"
#endif

// ThingSpeak Settings used to store and analyse data
const char* server = "api.thingspeak.com";
const char* Thingspeak_API_KEY = "APIKEY";
const char* Thingspeak_Channel_ID = "ChannelID";

//Constant configured for Geiger tube M4011
const float cpmConversation = 0.008120;

bool alerted = false;

const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 36000;
const int   daylightOffset_sec = 0;

int readings = 0;

IPAddress ip;
WiFiClientSecure secure_client;

const int kMaxLogBuffer = 1000;
String logData[kMaxLogBuffer];

IFTTTMaker ifttt(IFTTT_KEY, secure_client);

void(* resetFunc) (void) = 0; //declare reset function @ address 0

volatile unsigned long counts = 0;                       // Tube events
unsigned long cpm = 0;                                   // CPM
float uSvH = 0;
float caseTemp = 0;
float cpuTemp = 0;
unsigned long previousMillis;                            // Time measurement
unsigned long lastAlertMillis;                            // Time measurement
const int inputPin = 26;

void ISR_impulse() { // Captures count of events from Geiger counter board
  counts++;
}

void printLocalTime()
{
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
    Serial.println("rebooting");
    resetFunc();
    return;
  }
  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
}

void readFile(fs::FS &fs, const char * path)
{
    Serial.printf("Reading file: %s\n", path);

    File file = fs.open(path);
    if(!file){
        Serial.println("Failed to open file for reading");
        return;
    }

    Serial.print("Read from file: ");
    while (file.available()) {
      String line = file.readStringUntil('\n');
      Serial.println(line.c_str());
    }
    file.close();
}

void writeFile(fs::FS &fs, const char * path, const char * message)
{
    Serial.printf("Writing file: %s\n", path);

    File file = fs.open(path, FILE_WRITE);
    if(!file){
        Serial.println("Failed to open file for writing");
        return;
    }
    if(file.print(message)){
        Serial.println("File written");
    } else {
        Serial.println("Write failed");
    }
    file.close();
}

void appendFile(fs::FS &fs, const char * path, const char * message){
    Serial.printf("Appending to file: %s\n", path);

    File file = fs.open(path, FILE_APPEND);
    if(!file){
        Serial.println("Failed to open file for appending");
        return;
    }
    if(file.print(message)){
        Serial.println("Message appended");
    } else {
        Serial.println("Append failed");
    }
    file.close();
}

void deleteFile(fs::FS &fs, const char * path){
    Serial.printf("Deleting file: %s\n", path);
    if(fs.remove(path)){
        Serial.println("File deleted");
    } else {
        Serial.println("Delete failed");
    }
}

void RadCalc() {
  Serial.println("RadCalc");

  cpm = counts * MINUTE_PERIOD / LOG_PERIOD;
  uSvH = cpm * cpmConversation;
  
  counts = 0;
}

void CaseTempCalc() {
  byte i;
  byte present = 0;
  byte type_s;
  byte data[12];
  byte addr[8];
   
  if ( !ds.search(addr))
  {
    ds.reset_search();
    delay(250);
    return;
  }
   
  if (OneWire::crc8(addr, 7) != addr[7])
  {
    Serial.println("CRC is not valid!");
    return;
  }

  // the first ROM byte indicates which chip
  switch (addr[0])
  {
    case 0x10:
      type_s = 1;
      break;
    case 0x28:
      type_s = 0;
      break;
    case 0x22:
      type_s = 0;
      break;
    default:
      Serial.println("Device is not a DS18x20 family device.");
      return;
  }
   
  ds.reset();
  ds.select(addr);
  ds.write(0x44, 1); // start conversion, with parasite power on at the end
  delay(250);
  present = ds.reset();
  ds.select(addr);
  ds.write(0xBE); // Read Scratchpad
   
  for ( i = 0; i < 9; i++)
  {
    data[i] = ds.read();
  }
   
  // Convert the data to actual temperature
  int16_t raw = (data[1] << 8) | data[0];
  
  if (type_s) {
    raw = raw << 3; // 9 bit resolution default
    if (data[7] == 0x10)
    {
      raw = (raw & 0xFFF0) + 12 - data[6];
    }
  }
  else
  {
    byte cfg = (data[4] & 0x60);
    if (cfg == 0x00) raw = raw & ~7; // 9 bit resolution, 93.75 ms
    else if (cfg == 0x20) raw = raw & ~3; // 10 bit res, 187.5 ms
    else if (cfg == 0x40) raw = raw & ~1; // 11 bit res, 375 ms
  }
  
  caseTemp = (float)raw / 16.0;
}

void CPUTempCalc() {
  cpuTemp = (temprature_sens_read() - 32 ) / 1.8;
}

void ThresholdCheck() {
  //Serial.println("ThresholdCheck");
  
  //30 minutes between alerts if triggered
  if ((millis() - lastAlertMillis > 1800000) || lastAlertMillis == 0)
  {
    if (uSvH >= 0.75f )
      IFTTT( "AbnormalRadiationLevels", String(uSvH));
  }

  if (caseTemp >= 48.0f)
      IFTTT( "CaseTempHigh", String(caseTemp));
}

void WriteLogData() {

  Serial.println("WriteLogData");

  //get Time
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
    return;
  }
  
  char timestampBuffer[26];
  strftime(timestampBuffer, 26, "%Y-%m-%d %H:%M:%S +1000", &timeinfo);

  appendFile(SD, logFile, "{\"created_at\":\"");
  appendFile(SD, logFile, timestampBuffer);
  appendFile(SD, logFile, "\",\"field1\":\"");
  appendFile(SD, logFile, String(cpm).c_str());
  appendFile(SD, logFile, "\",\"field2\":\"");
  appendFile(SD, logFile, String(uSvH).c_str());
  appendFile(SD, logFile, "\",\"field3\":\"");
  appendFile(SD, logFile, String(caseTemp).c_str());
  appendFile(SD, logFile, "\",\"field4\":\"");
  appendFile(SD, logFile, String(cpuTemp).c_str());
  appendFile(SD, logFile, "\"}");
  appendFile(SD, logFile, "\n");
  
   //"created_at": "2018-04-22 10:26:2 +1100","field1": "2.0"},{"created_at": "2018-04-22 11:27:27 +1100""field1": "3.4"}   
   
}

bool PostDataOnline(String logData [], int n)
{
  String postData;

  //array start
  postData = "{\"write_api_key\": \"" + Thingspeak_API_KEY + "\",\"updates\": [";

  //build each record
  for(int i = 0; i < n; i++)
    postData += logData[i];

  postData += "]}";

  HTTPClient client;

  client.begin("http://api.thingspeak.com/channels/" + Thingspeak_Channel_ID + "/bulk_update.json");
  
  Serial.println("posting to api.thingspeak.com");
  
  client.addHeader("Content-Type", "application/json");
  client.addHeader("Cache-Control", "no-cache");
  
  Serial.println("");
  Serial.println(postData);
  Serial.println("");
  
  int httpCode = client.POST(postData);
  String payload = client.getString();

  Serial.print("httpCode : ");
  Serial.println(httpCode);   //Print HTTP return code
  Serial.print("payload : ");
  Serial.println(payload);    //Print request response payload
  
  client.end();  //Close connection

  if(httpCode != 202)
  {
    Serial.print("Error code");
    
    return false;
  }
  
  return true;
}

bool PostlogData(fs::FS &fs, const char * path) {

    Serial.println("PostlogData");

    StartWIFI();

    File file = fs.open(path);
    if(!file){
        Serial.println("Failed to open file for reading");
        return false;
    }

    bool status;
    int lines = 0;
    int posts = 0;
    
    const int kBatchSize = 180;

    while (file.available()) {
      String line = file.readStringUntil('\n');

      if(file.available() && lines+1 < kBatchSize)
        logData[lines] = line + ',';
      else
        logData[lines] = line;

      if(lines == kBatchSize)
      {
        if(posts != 0)
          delay(16000);
          
        PostDataOnline(logData, lines);
        posts++;
        
        lines = 0;
      } 
      else
      {
        lines++;
      }
    }

    //memory limit
    if(lines > kMaxLogBuffer)
      lines = kMaxLogBuffer;

    //post data
    if(lines > 0 )
    {
      delay(16000);
      PostDataOnline(logData, lines);
    }
    
    EndWIFI();

    file.close();

    return status;
}

bool StartWIFI()
{
  Serial.println("Connecting to WIFI");
  Serial.println("");
  
  WiFi.disconnect(false);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  unsigned long startMills = millis();
  
  while (WiFi.status() != WL_CONNECTED)
  {  
    unsigned long cMills = millis();
    
    delay(500);
    
    if (millis() - startMills > 60000 * 3) {
      Serial.println("Could not connect to WIFI. Aborting");
      Serial.println("rebooting");
      resetFunc();
      return false;
    }
  }

  delay(1500);

  return true;
}

void EndWIFI()
{
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  
  Serial.println("Disconnected from WIFI");
  Serial.println("");

  delay(1500);
}

void setup(){
    Serial.begin(115200);

    Serial.println("Booting");
    
    if(!SD.begin()){
        Serial.println("Card Mount Failed");
        return;
    }

    deleteFile(SD, logFile);

    previousMillis = millis();

    lastAlertMillis = 0;
    readings = 0;
    caseTemp = 0;
    uSvH = 0;
   
    //connect to WiFi
    Serial.printf("Connecting to %s for NTP setup", ssid);
  
    StartWIFI();
 
    //init and get the time
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    printLocalTime();
  
    EndWIFI();
  
    pinMode(inputPin, INPUT);                                                // Set pin for capturing Tube events
    interrupts();                                                            // Enable interrupts
    attachInterrupt(digitalPinToInterrupt(inputPin), ISR_impulse, FALLING); // Define interrupt on falling edge

    delay(1000);
}

void loop()
{
    //based on number of readings. default reading every 1 minute. so if this is 5 then every 5. minutes 
    if (readings >= ReadingsTransmitThreshold) 
    {
      Serial.println("Sending sensor data");

      PostlogData(SD, logFile);
      
      deleteFile(SD, logFile);
      
      readings = 0;
    }
    else if (millis() - previousMillis >= LOG_PERIOD) { // 1 minute

      Serial.println("Calculating sensor data");

      previousMillis = millis();
      
      RadCalc();

      CaseTempCalc();
    
      CPUTempCalc();
    
      ThresholdCheck();
    
      WriteLogData();

      readings++;
    }

    delay(1000);
}

void IFTTT(String event, String postValue) {

  Serial.println("IFTTT() " + event + " " + postValue);

  StartWIFI();
  
  if (ifttt.triggerEvent(event, String(postValue))) {
    Serial.println("Successfully sent to IFTTT");
  }
  else
  {
    Serial.println("IFTTT failed!");
  }

  EndWIFI();

  lastAlertMillis = millis();
}
