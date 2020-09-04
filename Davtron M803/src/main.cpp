#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <WiFiUdp.h>

#include "config.h"

#include <SPI.h>
#include <TFT_eSPI.h>

#include "font_dseg14_c_m_30.h"
#include "font_dseg14_c_m_60.h"
#include "orbitron_24.h"

/*
 * Issues:
 * sim/cockpit2/clock_timer/elapsed_time_seconds isn't sent over UDP
 * laminar/c172/knob_OAT isn't sent over UDP
 */

//---------------------------------------------------------------
//  Sample X-Plane UDP Communications for Arduino ESP8266 Variant
//  Copyright(c) 2019 by David Prue <dave@prue.com>
//
//  You may freely use this code or any derivitive for any
//  non-commercial purpose as long as the above copyright notice
//  is included.  For commercial use information email me.
//---------------------------------------------------------------

String ssid = SSID;
String pskey = PSKEY;

WiFiUDP udp;

IPAddress my_IP;
IPAddress multicastIP(239, 255, 1, 1); // Do not change this
IPAddress xplane_ip;

uint8_t beacon_major_version;
uint8_t beacon_minor_version;
uint32_t application_host_id;
uint32_t versionNumber;
uint32_t receive_port;
uint32_t role;
uint16_t port;
char xp_hostname[32];

#define STATE_IDLE 0
#define STATE_SEARCH 1
#define STATE_READY 2

char buffer[1024];

int state = STATE_IDLE;
unsigned int my_port = 3017;      // Could be anything, this just happens to be my favorite port number
unsigned int xplane_port = 49000; // Don't change this
unsigned int beacon_port = 49707; // or this...
int retry = 30;

#define GFXFF 1
#define CF_OL20 &Orbitron_Medium_20
#define CF_OL24 &Orbitron_Light_24
#define CF_OL32 &Orbitron_Light_32
#define DSEG14_30 &DSEG14_Classic_Mini_Regular_30
#define DSEG14_60 &DSEG14_Classic_Mini_Regular_60

TFT_eSPI tft = TFT_eSPI();

#define BACKGROUND_COLOR TFT_ORANGE

enum modes_timer { MODE_UT, MODE_LT, MODE_FT, MODE_ET };
enum modes_top { MODE_DEGF, MODE_DEGC, MODE_VOLTS };
int mode_timer = MODE_UT;
int mode_top = MODE_VOLTS;

float last_volts = 0.0;
float last_degC = 0.0;
float last_degF = 0.0;

int last_zulu_hours = 0;
int last_zulu_minutes = 0;

int last_lt_hours = 0;
int last_lt_minutes = 0;

int last_hobbs_hours = 0;
int last_hobbs_minutes = 0;

int last_elapsed_hours = 0;
int last_elapsed_minutes = 0;
int last_elapsed_seconds = 0;

int last_timer_running = 0;

// row 0 = top, row 1 = bottom
void displayText(int row, char *msg) {
  int x, y = 0;
  if (row == 0) {
    x = 100;
    y = 40;
  } else {
    x = 120;
    y = 120;
  }
  tft.setFreeFont(CF_OL24);
  tft.drawString(msg, x, y, GFXFF);
}

void clearIndicator() {
  // UT
  tft.fillTriangle(
    10, 120+24+5+3,
    10+(30/2), 120+24+3,
    10+30, 120+24+5+3,
    BACKGROUND_COLOR
  );
  // LT
  tft.fillTriangle(
    4+48, 120+24+5+3,
    4+48+(30/2), 120+24+3,
    4+48+30, 120+24+5+3,
    BACKGROUND_COLOR
  );
  // FT
  tft.fillTriangle(
    10, 120+24+24+5+3+10,
    10+(30/2), 120+24+24+3+10,
    10+30, 120+24+24+5+3+10,
    BACKGROUND_COLOR
  );
  // ET
  tft.fillTriangle(
    4+48, 120+24+24+5+3+10,
    4+48+(30/2), 120+24+24+3+10,
    4+48+30, 120+24+24+5+3+10,
    BACKGROUND_COLOR
  );
}

void refreshIndicator() {
  if (mode_timer == MODE_UT) {
    clearIndicator();
    tft.fillTriangle(
      10, 120+24+5+3,
      10+(30/2), 120+24+3,
      10+30, 120+24+5+3,
      TFT_BLACK
    );
  } else if (mode_timer == MODE_LT) {
    clearIndicator();
    tft.fillTriangle(
      4+48, 120+24+5+3,
      4+48+(30/2), 120+24+3,
      4+48+30, 120+24+5+3,
      TFT_BLACK
    );
  } else if (mode_timer == MODE_FT) {
    clearIndicator();
    tft.fillTriangle(
      10, 120+24+24+5+3+10,
      10+(30/2), 120+24+24+3+10,
      10+30, 120+24+24+5+3+10,
      TFT_BLACK
    );
  } else if (mode_timer == MODE_ET) {
    clearIndicator();
    tft.fillTriangle(
      4+48, 120+24+24+5+3+10,
      4+48+(30/2), 120+24+24+3+10,
      4+48+30, 120+24+24+5+3+10,
      TFT_BLACK
    );
  }
}

void refreshTop() {
  String str;
  switch (mode_top) {
    case MODE_DEGF:
      str = String(last_degF, 1);
      str += " F";
      break;
    case MODE_DEGC:
      str = String(last_degC, 1);
      str += " C";
      break;
    case MODE_VOLTS:
      str = String(last_volts, 1);
      str += " E";
      break;
  }

  tft.setFreeFont(DSEG14_60);
  tft.drawString(str, 60, 40, GFXFF);
}

void drawModes() {
  tft.setFreeFont(CF_OL20);
  tft.drawString("UT LT", 10, 120, GFXFF);
  tft.drawString("FT ET", 10, 120+24+10, GFXFF);
}

void refreshBottom() {
  char str[5];
  switch (mode_timer) {
    case MODE_UT:
      sprintf(str, "%02d:%02d", last_zulu_hours, last_zulu_minutes);
      break;

    case MODE_LT:
      sprintf(str, "%02d:%02d", last_lt_hours, last_lt_minutes);
      break;

    case MODE_FT:
      sprintf(str, "%02d:%02d", last_hobbs_hours, last_hobbs_minutes);
      break;

    case MODE_ET:
      if (last_elapsed_hours > 0) {
        sprintf(str, "%02d:%02d", last_elapsed_hours, last_elapsed_minutes);
      } else {
        sprintf(str, "%02d:%02d", last_elapsed_minutes, last_elapsed_seconds);
      }
      break;
  }

  Serial.print("display time: ");
  Serial.println(str);
  tft.setFreeFont(DSEG14_60);
  tft.drawString(str, 95, 135, GFXFF);
  refreshIndicator();
  drawModes();
}

void initDisplay() {
  tft.fillScreen(BACKGROUND_COLOR);
  tft.fillRect(0, 115, 320, 5, TFT_BLACK);
  tft.setFreeFont(CF_OL20);
  tft.setTextColor(TFT_BLACK, BACKGROUND_COLOR);
  drawModes();
  refreshIndicator();
}

void setup()
{
  retry = 30;
  Serial.begin(115200);
  Serial.println("\nX-Plane UDP Interface 0.9\n");

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pskey);

  Serial.print("Attempting Connection to Network: ");
  Serial.print(ssid);

  tft.begin();
  tft.setRotation(1);
  initDisplay();

  displayText(0, "init");

  // Wait for connection
  while ((retry-- > 0) && (WiFi.status() != WL_CONNECTED))
  {
    delay(500);
    Serial.print(".");
  }

  if (retry > 0)
  {
    my_IP = WiFi.localIP();
    Serial.print("connected.  My IP = ");
    Serial.println(my_IP);
  }
  else
  {
    Serial.println("Unable to connect");
    return;
  }

  Serial.print("Searching for X-Plane");
  retry = 30;
  udp.beginMulticast(my_IP, multicastIP, beacon_port);
  state = STATE_SEARCH;
}

void subscribe(char *dref, uint32_t freq, uint32_t index)
{
  struct
  {
    char dummy[3]; // For alignment
    char hdr[5] = "RREF";
    uint32_t dref_freq;
    uint32_t dref_en;
    char dref_string[400];
  } req __attribute__((packed));

  req.dref_freq = freq;
  req.dref_en = index;
  for (int x = 0; x < sizeof(req.dref_string); x++)
    req.dref_string[x] = 0x20;
  strcpy((char *)req.dref_string, (char *)dref);

  udp.beginPacket(xplane_ip, xplane_port);
  udp.write(req.hdr, sizeof(req) - sizeof(req.dummy));
  udp.endPacket();
  Serial.print("Subscribed to dref \"");
  Serial.print(dref);
  Serial.println("\"");
}

void loop()
{
  // If we were unable to connect to WiFi
  // try setting up again
  if (state == STATE_IDLE)
  {
    displayText(0, "Err");
    displayText(1, "Idle");
    setup();
    return;
  }

  if (state == STATE_SEARCH) {
    // displayText(0, "Err");
    // displayText(1, "search");

  }

  // See if we have a UDP Packet
  int packetSize = udp.parsePacket();

  if (!packetSize)
  {
    if (state == STATE_SEARCH)
    {
      Serial.print(".");
      delay(500);
      if (!retry--)
      {
        Serial.println("not found");
        delay(1000);
        setup();
        return;
      }
    }
  }
  else
  {
    switch (state)
    {
    case STATE_SEARCH:
      if (udp.destinationIP() == multicastIP)
      {
        char *buff = &buffer[1]; // For Alignment
        xplane_ip = udp.remoteIP();
        udp.read(buff, packetSize);
        beacon_major_version = buff[5];
        beacon_minor_version = buff[6];
        application_host_id = *((int *)(buff + 7));
        versionNumber = *((int *)(buff + 11));
        receive_port = *((int *)(buff + 15));
        strcpy(xp_hostname, &buff[21]);

        String version = String(versionNumber / 10000) + "." + String((versionNumber % 10000) / 100) + "r" + String(versionNumber % 100);
        String heading = " Found Version " + version + " running on " + String(xp_hostname) + " at IP ";
        Serial.print(heading);
        Serial.println(udp.remoteIP());

        state = STATE_READY;
        udp.begin(my_port);

        // Universal time (UT) Zulu
        subscribe("sim/cockpit2/clock_timer/zulu_time_hours", 1, 10);
        subscribe("sim/cockpit2/clock_timer/zulu_time_minutes", 1, 11);

        // Local Time (LT)
        subscribe("sim/cockpit2/clock_timer/local_time_hours", 1, 20);
        subscribe("sim/cockpit2/clock_timer/local_time_minutes", 1, 21);

        // Flight Time (FT)
        subscribe("sim/cockpit2/clock_timer/hobbs_time_hours", 1, 30);
        subscribe("sim/cockpit2/clock_timer/hobbs_time_minutes", 1, 31);

        // Elapsed Time (ET)
        subscribe("sim/cockpit2/clock_timer/elapsed_time_hours", 1, 40);
        subscribe("sim/cockpit2/clock_timer/elapsed_time_minutes", 1, 41);
        subscribe("sim/cockpit2/clock_timer/elapsed_time_seconds", 1, 42);

        // Outside Air Temperature in degC
        subscribe("sim/cockpit2/temperature/outside_air_temp_degc", 1, 50);

        // Outside Air Temperature in degF
        subscribe("sim/cockpit2/temperature/outside_air_temp_degf", 1, 51);

        // Battery Voltage
        subscribe("sim/cockpit2/electrical/battery_voltage_actual_volts[1]", 1, 55);

        // Start / Stop of Elapsed Time (RO)
        // 0 or 1
        subscribe("sim/cockpit2/clock_timer/timer_running", 2, 60);

        // Reset of Elapsed Time
        // sim/time/timer_elapsed_time_sec=0

        // Timer mode (RW)
        // UT=0 LT=1 FT=2 ET=3
        subscribe("sim/cockpit2/clock_timer/timer_mode", 20, 70);

        // OAT mode
        // degF=0, degC=1, volts=2
        subscribe("laminar/c172/knob_OAT", 100, 71);
      }
      break;

    case STATE_READY:
      char *buff = &buffer[3]; // For alignment
      udp.read(buff, packetSize);
      String type = String(buff).substring(0, 4);

      if (type == "RREF")
      {
        for (int offset = 5; offset < packetSize; offset += 8)
        {
          int code = *((int *)(buff + offset));
          float value = *((float *)(buff + offset + 4));

          switch (code)
          {

          case 10: 
            if (last_zulu_hours != value) {
              Serial.print("zulu_time_hours: ");
              Serial.println(String(value, 0));
              last_zulu_hours = value;
              refreshBottom();
            }
            break;
          case 11:
            if (last_zulu_minutes != value) {
              Serial.print("zulu_time_minutes: ");
              Serial.println(String(value, 0));
              last_zulu_minutes = value;
              refreshBottom();
            }
            break;

          case 20:
            if (last_lt_hours != value) {
              Serial.print("local_time_hours: ");
              Serial.println(String(value, 0));
              last_lt_hours = value;
              refreshBottom();
            }
            break;
          case 21:
            if (last_lt_minutes != value) {
              Serial.print("local_time_minutes: ");
              Serial.println(String(value, 0));
              last_lt_minutes = value;
              refreshBottom();
            }
            break;

          case 30:
            if (last_hobbs_hours != value) {
              Serial.print("hobbs_time_hours: ");
              Serial.println(String(value, 0));
              last_hobbs_hours = value;
              refreshBottom();
            }
            break;
          case 31:
            if (last_hobbs_minutes != value) {
              Serial.print("hobbs_time_minutes: ");
              Serial.println(String(value, 0));
              last_hobbs_minutes = value;
              refreshBottom();
            }
            break;

          case 40:
            if (last_elapsed_hours != value) {
              Serial.print("elapsed_time_hours: ");
              Serial.println(String(value, 0));
              last_elapsed_hours = value;
              refreshBottom();
            }
            break;
          case 41:
            if (last_elapsed_minutes != value) {
              Serial.print("elapsed_time_minutes: ");
              Serial.println(String(value, 0));
              last_elapsed_minutes = value;
              refreshBottom();
            }
            break;
          case 42:
            Serial.println(value);
            if (last_elapsed_seconds != value) {
              Serial.print("elapsed_time_seconds: ");
              Serial.println(String(value, 0));
              last_elapsed_seconds = value;
              refreshBottom();
            }
            break;

          case 50:
            if (last_degC != value) {
              Serial.print("outside_air_temp_degc: ");
              Serial.println(String(value, 0));
              last_degC = value;
              refreshTop();
            }
            break;
            
          case 51:
            if (last_degF != value) {
              Serial.print("outside_air_temp_degf: ");
              Serial.println(String(value, 0));
              last_degF = value;
              refreshTop();
            }
            break;

          case 55:
            if (last_volts != value) {
              Serial.print("battery_voltage_actual_volts: ");
              Serial.println(value);
              last_volts = value;
              refreshTop();
            }
            break;

          case 60:
            if (last_timer_running != value) {
              Serial.print("timer_running: ");
              Serial.println(String(value, 0));
              last_timer_running = value;
              refreshBottom();
            }
            break;

          case 70:
            // Set and redraw only if changed
            if (mode_timer != value) {
              Serial.print("timer_mode: ");
              Serial.println(String(value, 0));
              mode_timer = value;
              refreshIndicator();
              refreshBottom();
            }
            break;
            
          case 71:
            // Set and redraw only if changed
            if (mode_top != value) {
              Serial.print("oat_mode: ");
              Serial.println(String(value, 0));
              mode_top = value;
              refreshTop();
            }
            break;

            // case 42:
            //   Serial.print("Airspeed Dial: ");
            //   Serial.println(String(value, 0));
            //   break;

            // case 43:
            //   String theValue = "000" + String(value,0);
            //   theValue = theValue.substring(theValue.length() - 3);
            //   Serial.println("Heading Dial: " + theValue);
            //   break;
          }
        }
      }
    }
  }
}
