#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager
#include <ArduinoOTA.h>    //https://github.com/esp8266/Arduino/tree/master/libraries/ArduinoOTA
#define LILYGO_T5_V213
//#include <boards.h>
#include <GxEPD.h>
#include <GxDEPG0213BN/GxDEPG0213BN.h>
#include <GxIO/GxIO_SPI/GxIO_SPI.h>
#include <GxIO/GxIO.h>

#include "fonts/arial5pt7b.h"
#include "fonts/arial6pt7b.h"
#include "fonts/arial7pt7b.h"
#include "fonts/arial8pt7b.h"
#include "fonts/arial9pt7b.h"
#include "fonts/arial13pt7b.h"
#include <DateTime.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

#define SHOW_PERCENT_VOLTAGE true

#define VOLTAGE_DIVIDER_RATIO 7.0 //7.0 Varies from board to board
#define FG_COLOR GxEPD_BLACK
#define BG_COLOR GxEPD_WHITE

#define DEFALUT_FONT arial6pt7b
#define BIG_FONT arial9pt7b
#define SMALL_FONT arial6pt7b
#define BIGGEST_FONT arial13pt7b

#define EPD_BUSY  4  // to EPD BUSY
#define EPD_CS    5  // to EPD CS
#define EPD_RST  16  // to EPD RST
#define EPD_DC   17  // to EPD DC
#define EPD_SCK  18  // to EPD CLK
#define EPD_MISO -1  // MISO is not used, as no data from display
#define EPD_MOSI 23  // to EPD DIN

// #define USING_SOFT_SPI      //Uncomment this line to use software SPI
#if defined(USING_SOFT_SPI)
GxIO_Class io(EPD_SCLK, EPD_MISO, EPD_MOSI,  EPD_CS, EPD_DC,  EPD_RSET);
#else
GxIO_Class io(SPI,  EPD_CS, EPD_DC,  EPD_RST);
#endif

GxEPD_Class display(io, /*RST=*/ EPD_RST, /*BUSY=*/ EPD_BUSY); // default selection of (9), 7

#define DEBUG_ON 1

#ifdef DEBUG_ON
#define PRINTLN(...) Serial.println(__VA_ARGS__);
#define PRINT(...) Serial.print(__VA_ARGS__);
#else
#define PRINTLN(...) ;
#define PRINT(...) ;
#endif

#define FLASH_CS_PIN 11
#define BUILTIN_LED_PIN 19
#define VOLTAGE_PIN 35

#define SLEEP_DURATION_MIN  720  // 360 = 6 hours, 720 = 12 hours, 1440 == 24 hours. Sleep time in minutes
RTC_DATA_ATTR int bootCount = 0;

enum AlignmentType
{
  LEFT,
  RIGHT,
  CENTER
};

typedef struct {
  int x;
  int y;
  int h;
  int w;
} Bounds;

// Tewkesbury Borough Council REST API Endpoint to get the refuse dates
const char* host = "api-2.tewkesbury.gov.uk";
const int httpsPort = 443;
//This holds the JSON Response from the API call
const size_t capacity = JSON_ARRAY_SIZE(4) + JSON_OBJECT_SIZE(2) + 4 * JSON_OBJECT_SIZE(6) + 520;
//Your PostCode Here - remember - this is only for Tewkesbury Borough Council right now!
String postCode = "GL207RL";
//This is the REST API that Tewkesbury use
//String url = "/general/rounds/" + postCode + "/nextCollection";
//Latest version of the API that they use, including the unique property reference
String url = "https://api-2.tewkesbury.gov.uk/incab/rounds/200004323387/next-collection";
byte MonthDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}; //specify days in each month
String binToCollect = "";
String binColour = "";
String  time_str; 
String  date_str; // strings to hold time and date
int CurrentHour = 0;
int CurrentMin = 0;
int CurrentSec = 0;
long StartTime = 0;

void setup_pins()
{
  PRINT("INFO: Setup pins... ");
  pinMode(BUILTIN_LED_PIN, OUTPUT);
  //pinMode(FLASH_CS_PIN, OUTPUT);
  pinMode(VOLTAGE_PIN, INPUT);
  PRINTLN("OK");
}

void display_background()
{
  display.fillScreen(BG_COLOR);
  display.setTextColor(FG_COLOR);
  display.setFont(&DEFALUT_FONT);
  display.drawLine(0, 20, display.width(), 20, FG_COLOR);
  draw_string(display.width()-1, 15, "eBindicator", RIGHT);
  draw_battery(1, 20);
  draw_string(display.width() / 2, 15, date_str, CENTER);
  draw_string(display.width()-1, display.height()-1, WiFi.localIP().toString(), RIGHT);
  draw_string(1,display.height()-1,"Boot Count " + String(bootCount),LEFT);
}

void display_init()
{
  PRINT("INFO: Begin display initialisation ...");
  SPI.begin(EPD_SCK, EPD_MISO, EPD_MOSI, EPD_CS);
  display.init();
  display.setRotation(1); // Use 1 or 3 for landscape modes
  display_background();
  display.setFont(&BIGGEST_FONT);
  draw_string(display.width() / 2, display.height() / 2 + 15, "CONNECTING", CENTER);
  display.update();
  PRINTLN("DONE");
}
 
void setup() {
  StartTime = millis();
  ++bootCount;
  PRINTLN("----------------------");
  PRINTLN("Boot Number " + String(bootCount));
  setup_pins();
  display_init();
  
  WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP
  // it is a good practice to make sure your code sets wifi mode how you want it.

  // put your setup code here, to run once:
  Serial.begin(115200);
  
  //WiFiManager, Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wm;

  // reset settings - wipe stored credentials for testing
  // these are stored by the esp library
  //wm.resetSettings();

  // Automatically connect using saved credentials,
  // if connection fails, it starts an access point with the specified name ( "AutoConnectAP"),
  // if empty will auto generate SSID, if password is blank it will be anonymous AP (wm.autoConnect())
  // then goes into a blocking loop awaiting configuration and will return success result

  bool res;
  //res = wm.autoConnect(); // auto generated AP name from chipid
  //res = wm.autoConnect("AutoConnectAP"); // anonymous ap
  
  //sets timeout until configuration portal gets turned off
  //useful to make it all retry or go to sleep
  //in seconds
  wm.setTimeout(180);
  res = wm.autoConnect("eInk-Bindicator","password"); // password protected ap

  if(!res) {
      Serial.println("Failed to connect");
      display.fillScreen(BG_COLOR);
      display.setFont(&DEFALUT_FONT);
      display.drawLine(0, 20, display.width(), 20, FG_COLOR);
      draw_string(display.width()-1, 15, "eBindicator", RIGHT);
      draw_battery(1, 20);
      display.setFont(&BIGGEST_FONT);
      draw_string(display.width() / 2, display.height() / 2 + 15, "NO WIFI", CENTER);
      display.setFont(&DEFALUT_FONT);
      draw_string(1,display.height()-2,"Access Point: eInk-Bindicator ",LEFT);
      display.update();
      delay(5000);
      begin_sleep();

      //ESP.restart();
  } 
  else {
      //if you get here you have connected to the WiFi    
      Serial.println("connected...yeey :)");
  }

  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname("eInk-Bindicator");
  
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH)
      type = "sketch";
    else // U_SPIFFS
      type = "filesystem";

    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();
  PRINTLN("Ready OTA8");
  PRINT("IP address: ");
  PRINTLN(WiFi.localIP());

  display.setTextColor(FG_COLOR);
  display.setFont(&DEFALUT_FONT);
  draw_string(display.width()-1, display.height()-1, WiFi.localIP().toString(), RIGHT);

  if (setup_time() == true)
  {
    draw_string(display.width() / 2, 20, date_str, CENTER);
  }
  
  getBins();
  begin_sleep();
}

// Function to calculate the number of days in a month
int daysInMonth(int month, int year) {
  const int daysPerMonth[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  int days = daysPerMonth[month - 1];
  // Check for February in a leap year
  if (month == 2 && ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0))) {
    days = 29;
  }
  return days;
}

int daysBetweenDates(int startYear, int startMonth, int startDate, int endYear, int endMonth, int endDate) {
  int totalDays = 0;
    PRINT("Start Year: ");
  PRINTLN(startYear);
    PRINT("Start Month: ");
  PRINTLN(startMonth);
    PRINT("Start Date: ");
  PRINTLN(startDate);
    PRINT("End Year: ");
  PRINTLN(endYear);
    PRINT("End Month: ");
  PRINTLN(endMonth);
    PRINT("End Date: ");
  PRINTLN(endDate);

  // If it's the same month and year, calculate the difference directly
  if (startYear == endYear && startMonth == endMonth) {
    return endDate - startDate;
  }

  // Calculate days in the starting month from the start date to the end of the month
  totalDays += daysInMonth(startMonth, startYear) - startDate + 1;

  // Calculate days in the ending month from the beginning of the month to the end date
  totalDays += endDate;

  // Iterate through the months between the starting and ending months, adding the number of days in each month
  for (int year = startYear, month = startMonth + 1; !(year == endYear && month == endMonth);) {
    if (month > 12) {
      month = 1;
      year++;
    }
    totalDays += daysInMonth(month, year);
    month++;
  }

  return totalDays;
}

void getBins() {

    String displayBin = "";
    String displayDate = "";
    
    WiFiClientSecure client;
    HTTPClient http;
    http.useHTTP10(true);

    PRINT("connecting to ");
    PRINTLN(host);

    client.setInsecure();
    if (!client.connect(host, httpsPort)) {
      PRINTLN("connection failed");
      draw_string(display.width() / 2, display.height() / 2 + 20, "NO INTERNET", CENTER);
      display.update();
      delay(5000);
      ESP.restart();
    }

    PRINT("requesting URL: ");
    PRINTLN(url);

    // Send request
    http.begin(client, url);
    http.GET();
    String line = http.getString();

    DynamicJsonDocument jsonBuffer(capacity);
    auto error = deserializeJson(jsonBuffer, line);
    //auto error = deserializeJson(jsonBuffer, http.getStream());
    // Print the response
    //PRINTLN(line);
    PRINTLN("reply was:");
    PRINTLN("==========");
    PRINTLN(line);
    PRINTLN("==========");
    PRINTLN("closing connection");

    // Disconnect
    http.end();

    if (!error) {
      const char* status = jsonBuffer["status"]; // "OK"
      PRINT("Status: ");
      PRINTLN(status);
      JsonArray body = jsonBuffer["body"]; 

      String binType = "";
      String binColour = "";
      String collectionDateString = "";
      String collectionDateShortString = "";
      String nextBin = "";
      int collectionDate = 0;
      int collectionMonth = 0;
      int collectionYear = 0;
      String todaysDate = "";
      int todaysDay = 0;
      int todaysMonth = 0;
      int todaysYear = 0;

      display_background();

      todaysDate = DateTime.formatUTC(DateFormatter::DATE_ONLY).c_str();
      PRINTLN(todaysDate);
      todaysDay = todaysDate.substring(8, 10).toInt();
      todaysMonth = todaysDate.substring(5, 7).toInt();
      todaysYear = todaysDate.substring(0, 4).toInt();
      PRINT(todaysYear);
      PRINT("/");
      PRINT(todaysMonth);
      PRINT("/");
      PRINTLN(todaysDay);
      
      for (JsonVariant value : body) {
        //loopCounter++;
        binType = value["collectionType"].as<String>() ;//.as<char*>();
        collectionDateString = value["LongDate"].as<String>();
        collectionDateShortString = value["NextCollection"].as<String>();
        collectionDate = collectionDateShortString.substring(8, 10).toInt(); // Extracting the day part
        collectionMonth = collectionDateShortString.substring(5, 7).toInt(); // Extracting the month part
        collectionYear = collectionDateShortString.substring(0, 4).toInt(); // Extracting the year part
        PRINTLN("BIN: " + binType);
        if (binType != "Food" && binType != "Garden")
        {
          PRINTLN("Working on current bin: " + binType);
          if (binType == "Refuse") {
            binColour = "GREEN";
          } else {
            binColour = "BLUE";
          }
          PRINTLN("Colour: " + binColour);

          int days = daysBetweenDates(todaysYear, todaysMonth, todaysDay, collectionYear, collectionMonth, collectionDate);
          PRINT("Number of days between the two dates: ");
          PRINTLN(days);
          if (days <= 7 && days > 0) {           //Leave the bin colour for todays collection just in case you forgot to put the bin out!
             displayBin=binColour;
             displayDate=collectionDateString;
          }
        }
      }
    }

    PRINT("Chosen Bin: ");
    PRINTLN(displayBin);
    PRINT("To Be Collected On: ");
    PRINTLN(displayDate);
    display.setFont(&BIGGEST_FONT);
    display.setTextColor(FG_COLOR);
    draw_string(display.width() / 2, 60, displayBin, CENTER);
    display.setFont(&BIG_FONT);
    draw_string(display.width() / 2, 90,displayDate, CENTER);
    display.update();
}


float read_battery_voltage()
{
  PRINT("INFO: Read battery voltage... ");
  float voltageRaw = 0;
  for (int i = 0; i < 10; i++)
  {
    voltageRaw += analogRead(VOLTAGE_PIN);
    delay(10);
  }
  voltageRaw /= 10;
  PRINTLN("DONE");
  return voltageRaw;
}

void draw_battery(const int x, int y)
{
  float minLiPoV = 3.4;
  float maxLiPoV = 4.01;
  float percentage = 1.0;
  // analog value = Vbat / 2
  float voltageRaw = read_battery_voltage();
  // voltage = divider * V_ref / Max_Analog_Value
  float voltage = VOLTAGE_DIVIDER_RATIO * voltageRaw / 4095.0;
  if (voltage > 1)
  { // Only display if there is a valid reading
    PRINTLN("INFO: Voltage Raw = " + String(voltageRaw));
    PRINTLN("INFO: Voltage = " + String(voltage));
    
    if (voltage >= maxLiPoV)
      percentage = 1;
    else if (voltage <= minLiPoV)
      percentage = 0;
    else
      percentage = (voltage - minLiPoV) / (maxLiPoV - minLiPoV);
      
    display.drawRect(x, y - 12, 19, 10, FG_COLOR);
    display.fillRect(x + 2, y - 10, 15 * percentage, 6, FG_COLOR);
    display.setFont(&DEFALUT_FONT);
    if (SHOW_PERCENT_VOLTAGE)
    {
      draw_string(x + 21, y - 4, String(percentage * 100, 0) + "%", LEFT);
    } else {
      draw_string(x + 21, y - 4, String(voltage, 1) + "v", LEFT);
    }
  }
}

Bounds draw_string(int x, int y, String text, AlignmentType alignment)
{
  int16_t x1, y1; //the bounds of x,y and w and h of the variable 'text' in pixels.
  uint16_t w, h;
  display.setTextWrap(false);
  display.getTextBounds(text, x, y, &x1, &y1, &w, &h);
  if (alignment == RIGHT) 
  {
    x = x - w;
    x1 = x1 - w;
  }
  if (alignment == CENTER)
  {
    x = x - w / 2;
    x1 = x1 - w / 2;
  }
  display.setCursor(x, y);
  Bounds b;
  b.x = x1;
  b.y = y1;
  b.w = w;
  b.h = h;
  display.print(text);
  return b;
}

bool update_local_time()
{
  PRINT("INFO: Updating local time... ");
  struct tm timeinfo;
  char time_output[30], day_output[30], update_time[30];
  while (!getLocalTime(&timeinfo, 5000))
  { // Wait for 5-sec for time to synchronise
    PRINTLN("ERROR: Failed to obtain time");
    return false;
  }
  CurrentHour = timeinfo.tm_hour;
  CurrentMin = timeinfo.tm_min;
  CurrentSec = timeinfo.tm_sec;
  //See http://www.cplusplus.com/reference/ctime/strftime/
  PRINT(&timeinfo, "%a %b %d %Y %H:%M"); // Displays: Saturday, June 24 2017 14:05:49

  strftime(day_output, sizeof(day_output), "%a %b-%d-%Y", &timeinfo); // Creates  'Sat May-31-2019'
  strftime(update_time, sizeof(update_time), "%r", &timeinfo);        // Creates: '@ 02:05:49pm'
  sprintf(time_output, "%s", update_time);

  date_str = day_output;
  time_str = time_output;
  PRINTLN(" DONE");
  return true;
}


bool setup_time()
{
  PRINT("INFO: Setup time... ");
  configTime(0, 3600, "0.uk.pool.ntp.org", "time.nist.gov"); //(gmtOffset_sec, daylightOffset_sec, ntpServer)
  setenv("TZ", "GMT", 1);                       //setenv()adds the "TZ" variable to the environment with a value TimeZone, only used if set to 1, 0 means no change
  tzset();                                                              // Set the TZ environment variable
  delay(100);
  PRINTLN("DONE");
  return update_local_time();
}

void begin_sleep()
{
  display.powerDown();
  long SleepTimer = (SLEEP_DURATION_MIN * 60 - ((CurrentMin % SLEEP_DURATION_MIN) * 60 + CurrentSec)); //Some ESP32 are too fast to maintain accurate time
  esp_sleep_enable_timer_wakeup((SleepTimer + 20) * 1000000LL);  //did have LL at the end of 100000                                      // Added 20-sec extra delay to cater for slow ESP32 RTC timers

  PRINTLN("INFO: Entering " + String(SleepTimer) + "-secs of sleep time");
  PRINTLN("INFO: Awake for : " + String((millis() - StartTime) / 1000.0, 3) + "-secs");
  PRINTLN("INFO: Starting deep-sleep period...");

  gpio_deep_sleep_hold_en();
  esp_wifi_stop();
  esp_deep_sleep_start(); // Sleep for e.g. 30 minutes
}

void loop() {
    // put your main code here, to run repeatedly:   
}
