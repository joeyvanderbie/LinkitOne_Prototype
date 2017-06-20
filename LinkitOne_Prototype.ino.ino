#include <Wire.h>
#include <I2Cdev.h>
#include <MPU6050.h>

// The version for the alternate accelerometer

#include "LTask.h"
// Accelerometer

// Batter level and state
#include <LBattery.h>
// SD card
#include <LSD.h>
// File management
#include <LStorage.h>
// GSM/GPRS connection
#include <LGPRS.h>
// Sending data to web server
#include <LGPRSClient.h>
// UDP connection (used for timestamp)
#include <LGPRSUdp.h>
// Date and time
#include <LDateTime.h>

#define OUTPUT_READABLE_ACCELGYRO

// Define Pin to power relay
#define LED_BUILTIN 0

// Device name (for each device individual)
#define DEVICE_NAME "cruyff_court_01"

// SD card file names
// temporarly contains information in JSON format style
// Line format -> {"time": "YYYY-MM-DD HH:MM:SS", "state": 1, "battery": 100}
#define CACHE_FILE "cache.txt"
// contains all information in excel format style
// Line format -> DD.MM.YYYY; HH:MM:SS; 1; 100
#define LOCALSTORAGE_FILE "local.csv"
// contains times of battery charging in excel format style
#define BATTERY_FILE "battery.csv"

// Data file for SD file write/read operations
LFile dataFile;

// GPRS client for server communication
LGPRSClient client;

// Server URL
#define SERVER_URL "bootcamp01.000webhostapp.com"
// Uncomment for testing purpose
// #define SERVER_URL "www.httpbin.org"

// Time server
#define TIME_SERVER "0.nl.pool.ntp.org"
// GPRS UDP package for the time server
LGPRSUDP Udp;
// local port to listen for UDP packets
unsigned int localPort = 2390;
// NTP time stamp is in the first 48 bytes of the message
const int NTP_PACKET_SIZE = 48;
//buffer to hold incoming and outgoing packets
byte packetBuffer[NTP_PACKET_SIZE];

// datetimeinfo variable that should hold the current time after the setup
datetimeInfo currentTime;

//variable accelgyro is an instance of the MPU6050 library (accelerometer)
MPU6050 accelgyro;

// Intervals to proceed with periodical operations
// 5 minutes (300 seconds, 300000 milliseconds)
const long millisIntervalStore = 300000;
// 60 minutes (3600 seconds, 3600000 milliseconds)
const long millisIntervalSend = 3600000;
// 60 minutes (3600 seconds, 3600000 milliseconds)
const long millisIntervalBattery = 3600000;

// long value for millis operations
unsigned long previousMillisStore = 0;
unsigned long previousMillisSend = 0;
unsigned long previousMillisBattery = 0;

// Boolean that shows whether usage/vibration was detected
boolean usageDetected = false;

// Initial previous measured value
int16_t previousValue = 0;
int16_t previousValueX = 0;
int16_t previousValueY = 0;

// treshold when acceleration is triggered as movement/usage
const int acceleromationTreshold = 10000;

// manual time variables (IMPORTANT: needs to be manually updated before placing sensor)
// TODO: Find a funtion to calculate the date out of the UNIX timestamp
int year = 2017;
int mon = 6;
int day = 15;
int hour = 0;
int min = 0;
int sec = 0;


void setup() {
  Wire.begin();   //begin I2c
  Serial.begin(9600);

  // Set relay pin to output
  pinMode(LED_BUILTIN, OUTPUT);
  // Turn relay pin off
  digitalWrite(LED_BUILTIN, LOW);

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
  Serial.println("Initializing I2C devices...");
  accelgyro.initialize();
  Serial.println(accelgyro.testConnection() ? "MPU6050 connection successful" : "MPU6050 connection failed");

  // Set current timestamp after setup finished
  previousMillisStore = millis();
  previousMillisSend = millis();
  previousMillisBattery = millis();
  
  // Send previous measured data at first start
  sendData();
}

void loop() {

  // Get timestamp of current loop
  LDateTime.getTime(&currentTime);

  // Get current millis values for time comparisons
  unsigned long currentMillisStore = millis();
  unsigned long currentMillisSend = millis();
  unsigned long currentMillisBattery = millis();

  int16_t currentValue; // currently z axis values for upright position
  int16_t y, x;
  //read the accelerometer values and store them in variables x,y,currentValue
  accelgyro.getAcceleration(&x, &y, &currentValue);

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

    // Send data to web server
    sendData();
  }

  // Check battery level every x minutes/seconds
  if (currentMillisBattery - previousMillisBattery >= millisIntervalBattery) {
    // save the last time the battery was checked
    previousMillisBattery = currentMillisBattery;

    // Trigger the power bank with the relay if battery level gets below 33%
    if (LBattery.level() <= 33) {
      Serial.println("Battery level dropped below 33%, triggering the power bank...");
      // turn the LED/relay pin on by setting voltage to HIGH
      digitalWrite(LED_BUILTIN, HIGH);
      // wait for one second
      delay(1000);
      // turn the LED/relay pin off by setting voltage to LOW
      digitalWrite(LED_BUILTIN, LOW);

      // Print battery loading to file on SD card
      dataFile = LSD.open(BATTERY_FILE, FILE_WRITE);
      if (dataFile) {
        String returnString = "";
        returnString += getDateString(currentTime);
        returnString += " ";
        returnString += getTimeString(currentTime);
        returnString += "; battery is getting charged";
        dataFile.println(returnString);
        dataFile.close();
      }
    }
  }



  // Update the previous measured value with the current measured value
  previousValue = currentValue;
  //  previousValueX = x;
  //  previousValueY = y;

  // Wait to not overload
  delay(700);
}


// Write current Timestamp and usage to the SD card
//__________________________________________________________________
void writeToStorage() {

  // Create strings that will be written to the SD card files
  String cacheString = buildJsonString();
  String localString = buildExcelString();

  // Open cache file with write access
  Serial.println("Trying to access files on SD card");
  dataFile = LSD.open(CACHE_FILE, FILE_WRITE);
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


// Build JSON String and send that string via POST method to server
//__________________________________________________________________
void sendData() {

  // create payload to send to server
  String payload = buildJson();

  // Try to connect to the webserver
  while (!client.connect(SERVER_URL, 80))
  {
    Serial.println("Retrying to connect...");
    delay(100);
    // TODO: Counter if there's a connection error
  }
  Serial.println("Client reconnected!");

  Serial.println("Sending data to webserver:");
  Serial.println(payload);

  // Build HTTP POST request
  client.print("POST /mass_insert.php");
  // client.print("POST /post");
  client.println(" HTTP/1.1");
  client.print("Host: ");
  client.println(SERVER_URL);
  client.print("User-Agent: ");
  client.println(DEVICE_NAME);
  client.println("Connection: close");
  client.println("Content-Type: application/json; charset=UTF-8");
  client.print("Content-Length: ");
  client.println(payload.length());
  client.println();
  client.println(payload);
  client.println();

  Serial.println("Data sent, waiting for response...");

  // read/print server response
  while (client.connected()) {
    while (client.available()) {
      Serial.write(client.read());
      // TODO: Check for 'SUCCESS' in response
    }
  }
  client.stop();

  // TODO: Only if success response from server -> emptyCache, else keep cache contents
  emptyCache();
}



//________________________
// HELPERS
//________________________

// Return string in JSON style format containing the contents of the cache file
//__________________________________________________________________
String buildJson() {

  // Start return string by opening square brackets
  String returnString = "[";
  // returnString += "\n";

  // Open file from sd card
  dataFile = LSD.open(CACHE_FILE);

  if (dataFile) {
    dataFile.seek(0);
    // read from the file until there's nothing else in it:
    while (dataFile.available()) {
      char dataFileLine = dataFile.read();
      if (dataFileLine == '\n') {
        returnString += ",";
      } else {
        returnString += dataFileLine;
      }
    }

    // Add device name at the end of JSON array
    returnString += "{\"name\": \"";
    returnString += DEVICE_NAME;
    returnString += "\"}";

    // close the file:
    dataFile.close();
  } else {
    // if the file didn't open, print an error:
    Serial.println("buildJson(): error opening cache file");
  }

  // End return string by closing square brackets
  returnString += "]";

  Serial.println("JSON string for post request created:");

  // Return the created JSON format style string
  return returnString;
}


// Empty (or delete) the cache file on the SD card
//__________________________________________________________________
void emptyCache() {
  // TODO: Clear the chache file contents (e.g. when sending to server was successful)

  if (LSD.remove(CACHE_FILE)) {
    delay(200);
    dataFile = LSD.open(CACHE_FILE, FILE_WRITE);
    if (dataFile) {
      Serial.println("Old cache file deleted, new cache file created");
      dataFile.close();
    } else {
      Serial.println("Could not create new cache file!");
    }
  } else {
    Serial.println("Cache file could not be removed!");
  }
}


// Return one line string for the cache.txt file
//__________________________________________________________________
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
  if (usageDetected) {
    returnString += "1";
  } else {
    returnString += "0";
  }
  returnString += ",";
  returnString += "\"battery\": ";
  returnString += LBattery.level();
  returnString += "}";

  return returnString;
}

// Return one line string for the local.csv file
//__________________________________________________________________
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
//__________________________________________________________________
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


// Return readable date string for timestamp
//__________________________________________________________________
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


// Return readable time string for timestamp
//__________________________________________________________________
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



//________________________
// NTP time server
//________________________


// Connect to udp/time server
//__________________________________________________________________
void getNtpTime() {

  // Tutorial: https://github.com/brucetsao/techbang/blob/master/201511/LinkIt-ONE-IDE/hardware/arduino/mtk/libraries/LGPRS/examples/GPRSUdpNtpClient/GPRSUdpNtpClient.ino

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

    // add 2 hours (7200 seconds) to get Amsterdam timezone
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


// Send an NTP request to the time server at the given address
//__________________________________________________________________
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

  // all NTP fields have been given values, now you can send a packet requesting a timestamp:
  // NTP requests are to port 123
  Udp.beginPacket(TIME_SERVER, 123);
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}

