#include "LTask.h"
// Accelerometer
#include <ADXL345.h>
// Batter level and state
#include <LBattery.h>
// SD card
#include <LSD.h>
// GSM/GPRS connection
#include <LGPRS.h>
// Sending data to web server
#include <LGPRSClient.h>
// UDP connection (currently used for timestamp)
#include <LGPRSUdp.h>
// Date and time
#include <LDateTime.h>

#define DEVICE_NAME "cruyff_court_01"

// SD card file names
// temporarly contains information in JSON format style
// Line format -> {"time": "YYYY-MM-DD HH:MM:SS", "state": 1, "battery": 100}
#define CACHE_FILE "cache.txt"
// contains all information in excel format style 
// Line format -> DD.MM.YYYY; HH:MM:SS; 1; 100
#define LOCALSTORAGE_FILE "local.csv"

// GPRS client for server communication
LGPRSClient client;

// Server URL
#define SERVER_URL "bootcamp01.000webhostapp.com"

// Time server
#define TIME_SERVER "0.nl.pool.ntp.org" // a list of NTP servers: http://tf.nist.gov/tf-cgi/servers.cgi
// GPRS UDP package for the time server
LGPRSUDP Udp;
unsigned int localPort = 2390;      // local port to listen for UDP packets
const int NTP_PACKET_SIZE = 48; // NTP time stamp is in the first 48 bytes of the message
byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets

// datetimeinfo variable that should hold the current time after the setup
datetimeInfo currentTime;

//variable adxl is an instance of the ADXL345 library (accelerometer)
ADXL345 adxl;

// Intervals to proceed with periodical operations
// 10 seconds (will be 5 minutes in final version)
const long millisIntervalStore = 10000;
// 1800 seconds (30 minutes)
const long millisIntervalSend = 180000;

// long value for millis operations
unsigned long previousMillisStore = 0;
unsigned long previousMillisSend = 0;

// Boolean that shows whether usage/vibration was detected
boolean usageDetected = false;

// Initial previous measured value
int previousValue = 0;
int previousValueX = 0;
int previousValueY = 0;

// treshold when acceleration is triggered as movement/usage
const int acceleromationTreshold = 20;

// manual time variables (IMPORTANT: needs to be manually updated before placing sensor)
int year = 2017;
int mon = 5;
int day = 31;
int hour = 0;
int min = 0;
int sec = 0;


void setup() {
  Serial.begin(9600);

  // Initialize the SD card
  Serial.print("Initializing SD card...");
  LSD.begin();
  Serial.println("card initialized.");
  Serial.println();

  // Connect to GPRS network
  while (!LGPRS.attachGPRS("data.lycamobile.nl", "lmnl", "plus"))
  {
    delay(1000);
    Serial.println("retry connecting to GPRS network");
  }
  Serial.println("Connected to GPRS network");

  // Get time via Udp connection from NTP server
  getNtpTime();

  // Set time (ntp time if udp request successful or manual time)
  setTime();

  // Power on the accelerometer
  adxl.powerOn();

}

void loop() {

  // Get timestamp of current loop
  LDateTime.getTime(&currentTime);

  unsigned long currentMillisStore = millis();
  unsigned long currentMillisSend = millis();

  int currentValue; // currently z axis values for upright position
  int y, x;
  //read the accelerometer values and store them in variables x,y,z
  adxl.readXYZ(&x, &y, &currentValue);

  // Print all measured differences for treshold testing purposes
  Serial.print("Z: ");
  Serial.println(abs(currentValue - previousValue));
  //  Serial.print("X: ");
  //  Serial.println(abs(x - previousValueX));
  //  Serial.print("Y: ");
  //  Serial.println(abs(y - previousValueY));
  //  Serial.println();


  // If no usage was detected, still look for it.
  if (!usageDetected) {
    if (abs(currentValue - previousValue) > acceleromationTreshold) {
      usageDetected = true;
      // Print timestamp of usage measurement
      Serial.print(getDateString(currentTime));
      Serial.println(": Usage detected! No more measurements till next update");
      Serial.println();
    }
  }

  // Store (measured state) data every x minutes/seconds
  if (currentMillisStore - previousMillisStore >= millisIntervalStore) {
    // save the last time an update was sent
    previousMillisStore = currentMillisStore;

    // Write to SD card
    writeToStorage();
    // Reset detection boolean
    usageDetected = false;
  }

  // Send data every x minutes/seconds
  if (currentMillisSend - previousMillisSend >= millisIntervalSend) {
    // save the last time an update was sent
    previousMillisSend = currentMillisSend;

    // Send data to web server (still in test state)
    sendData();
  }

  // Update the previous measured value with the current measured value
  previousValue = currentValue;
  //  previousValueX = x;
  //  previousValueY = y;

  // Wait to not overload
  delay(700);

}


// Write current Timestamp and usage to the SD card
void writeToStorage() {

  // Create strings that will be written to the SD card files
  String cacheString = buildJsonString();
  String localString = buildExcelString();
 
  // Open cache file with write access
  Serial.println("Trying to access files on SD card");
  LFile dataFile = LSD.open(CACHE_FILE, FILE_WRITE);
  Serial.println("Cache file opened");
  // if the file is available, write to it:
  if (dataFile) {
    dataFile.println(cacheString);
    dataFile.close();
    // print to the serial port too:
    Serial.println(cacheString);
    Serial.println("Cache file written and closed");
  }
  // if the file isn't open, pop up an error:
  else {
    Serial.println("error opening cache file");
  }

  // Open local storage file with write access
  dataFile = LSD.open(LOCALSTORAGE_FILE, FILE_WRITE);
  Serial.println("Local storage file opened");
  // if the file is available, write to it:
  if (dataFile) {
    dataFile.println(localString);
    dataFile.close();
    // print to the serial port too:
    Serial.println(localString);
    Serial.println("Local storage file written and closed");
  }
  // if the file isn't open, pop up an error:
  else {
    Serial.println("error opening local storage file");
  }

  Serial.println();
}

void sendData() {
  // TODO: Implement building JSON String and send that string via POST method to server
  
  Serial.println("Sending data to webserver...");
  buildJson();

  // TODO: If success response from server -> emptyCache
}




//________________________
// HELPERS

String buildJson(){
  // TODO: create JSON (string) file for server from data of cache file
  
}

// Empty (or delete) the cache file on the SD card
void emptyCache() {
  // TODO: Clear the chache file contents (e.g. when sending to server was successful)
}

// Returns one line string for the cache.txt file
String buildJsonString() {
    
  String returnString;
  returnString += "{";
  returnString += "\"time\": ";
  returnString += "\"";
  returnString += getDateString(currentTime);
  returnString += " ";
  returnString += getTimeString(currentTime);
  returnString += "\"";
  returnString += ",";
  returnString += "\"state\": ";
  returnString += "\"";
  if (usageDetected) {
    returnString += "1";
  } else {
    returnString += "0";
  }
  returnString += "\"";
  returnString += ",";
  returnString += "\"battery\": ";
  returnString += "\"";
  returnString += LBattery.level();
  returnString += "\"";
  returnString += "}";
  
  return returnString;
}

// Returns one line string for the local.csv file
String buildExcelString() {

  // String that gets written to local storage and cache files
  String returnString;
  
  returnString += getDateString(currentTime);
  returnString += " ";
  returnString += getTimeString(currentTime);
  returnString += ";";
  if (usageDetected) {
    returnString += "1";
  } else {
    returnString += "0";
  }
  returnString += ";";
  returnString += LBattery.level();

  return returnString;
}


// Set time from global time variables
void setTime() {
  // Set time stamp manually
  datetimeInfo now;
  now.year = year;
  now.mon = mon;
  now.day = day;
  now.hour = hour;
  now.min = min;
  now.sec = sec;
  LDateTime.setTime(&now);
}

// Returns readable date string for timestamp
String getDateString(datetimeInfo dti) {
  // Output format: "YYYY-MM-DD"
  String dateStr;
  dateStr += dti.year;
  dateStr += "-";
  dateStr += dti.mon;
  dateStr += "-";
  dateStr += dti.day;
  return dateStr;
}

// Returns readable time string for timestamp
String getTimeString(datetimeInfo dti) {
  // Output format: "HH:MM:SS"
  String timeStr;
  timeStr += dti.hour;
  timeStr += ":";
  timeStr += dti.min;
  timeStr += ":";
  timeStr += dti.sec;
  return timeStr;
}

// NTP time server stuff
//____________________________________________

// Connect to udp/time server, https://github.com/brucetsao/techbang/blob/master/201511/LinkIt-ONE-IDE/hardware/arduino/mtk/libraries/LGPRS/examples/GPRSUdpNtpClient/GPRSUdpNtpClient.ino
void getNtpTime() {
  Serial.print("\nStarting connection to time server...");
  while (!Udp.begin(localPort)) {
    Serial.println("retry begin");
    delay(1000);
  }
  Serial.println("connected");

  sendNTPpacket();
  // wait to see if a reply is available
  delay(1000);

  // If empty or false packet was received, try again
  while (Udp.parsePacket() == 0) {
    delay(1000);
    Serial.println("Received empty or false UDP packet, retry...");
    sendNTPpacket();
  }

  Serial.println( Udp.parsePacket() );
  if ( Udp.parsePacket() ) {
    Serial.println("Udp packet received");
    // We've received a packet, read the data from it
    memset(packetBuffer, 0xcd, NTP_PACKET_SIZE);
    Udp.read(packetBuffer, NTP_PACKET_SIZE); // read the packet into the buffer

    //the timestamp starts at byte 40 of the received packet and is four bytes,
    // or two words, long. First, esxtract the two words:
    unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
    unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
    // combine the four bytes (two words) into a long integer
    // this is NTP time (seconds since Jan 1 1900):
    unsigned long secsSince1900 = highWord << 16 | lowWord;
    Serial.print("Seconds since Jan 1 1900 = " );
    Serial.println(secsSince1900);

    // subtract 2 hours to get Amsterdam timezone
    secsSince1900 = secsSince1900 + 7200;

    // now convert NTP time into everyday time:
    Serial.print("Unix time = ");
    // Unix time starts on Jan 1 1970. In seconds, that's 2208988800:
    const unsigned long seventyYears = 2208988800UL;
    // subtract seventy years:
    unsigned long epoch = secsSince1900 - seventyYears;
    // print Unix time:
    Serial.println(epoch);

    // print the hour, minute and second:
    Serial.print("The UTC time is ");       // UTC is the time at Greenwich Meridian (GMT)
    // Set global hour variable
    hour = (epoch  % 86400L) / 3600;

    Serial.print(hour); // print the hour (86400 equals secs per day)
    Serial.print(':');
    if ( ((epoch % 3600) / 60) < 10 ) {
      // In the first 10 minutes of each hour, we'll want a leading '0'
      Serial.print('0');
    }
    // Set global minute variable
    min = (epoch  % 3600) / 60;
    Serial.print(min); // print the minute (3600 equals secs per minute)
    Serial.print(':');
    if ( (epoch % 60) < 10 ) {
      // In the first 10 seconds of each minute, we'll want a leading '0'
      Serial.print('0');
    }
    // Set global second variable
    sec = epoch % 60;
    Serial.println(sec); // print the second
  }
  Serial.println();
}

// send an NTP request to the time server at the given address
unsigned long sendNTPpacket() {
  Serial.println("sendNTPpacket");
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;

  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  Udp.beginPacket(TIME_SERVER, 123); //NTP requests are to port 123
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}

