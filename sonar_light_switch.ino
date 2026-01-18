/*
 * Sonar Light Switch
 * 
 * Changelog:
 * v3.0: 2026 Jan. 18: Reliability and diagnostic overhaul
 *       - include day/night logic natively via NTP and Dusk2Dawn
 *       - eliminates reliance on external smart switch (reduces reboots)
 *       - adds heartbeats to keep ARP/Kasa and SSL/Google connections warm
 *       - adds (optional) diagnostic LEDs (2, 4, 7) for debugging
 *       - non-blocking wifi code to prevent sonar lag during signal drops
 *       - strategic logging for system boot, wifi recovery, and NTP failures
 *       - switch to subtraction logic for millis() to avoid overflow issues
 *       - defer logging to Google form until a quiet period
 *       - change porch light on algo
 *       - added porch light off algo
 * v2.2: 2023 June 8: improve logging and clean up code
 * v2.1: 2023 June 7; add logging to Google Sheet and clean up code
 * v2.0: 2021 Dec. 1; use two range sensors to turn on light from either 
 *       direction, and switch to wifi Arduino to turn on porch light when 
 *       going up
 * v1.1: 2021 Nov. 12; eliminate daylight sensor logic (relies on a smart 
 *       switch to turn power off during daylight)
 * v1.0: 2013 Oct. 18; use light sensor for daylight detection
 * 
 * @date:    January 18, 2026
 * @version: 3.0
 * Copyright (c) 2013, 2021, 2023, 2026 Michael Kwun
 *
 *  Permission is hereby granted, free of charge, to any person obtaining
 *  a copy of this software and associated documentation (the "Software"), 
 *  to deal in the Software without restriction, including without 
 *  limitation the rights to use, copy, modify, merge, publish, 
 *  distribute, sublicense, and/or sell copies of the Software, and to
 *  permit persons to whom the Software is furnished to do so, subject to
 *  the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included
 *  in all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 *  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 *  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 *  IN NO EVENT SHALL THE AUTHOR OR COPYRIGHT HOLDER BE LIABLE FOR ANY
 *  CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 *  TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 *  SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 *
 *  I use this sketch for an outdoor light in the middle of a stairway. 
 *  There's also a light on a porch at the top of a second, higher run of 
 *  stairs. Both lights are connected to TP-Link Kasa smart switches 
 *  (HS-200). The stairway switch is generally always on, and the sketch
 *  then controls the actual light.
 *  
 *  The sketch is designed for use with an Arduino Uno WiFi rev 2, so we have 
 *  a network interface. We also use two ultrasonic ranging sensors (HC-SR04 
 *  clones) that detect bodies below and above the stairway light. A body at 
 *  either sensor will trigger the stairway light. The light stays on until 
 *  no body is detected at either sensor for a set period of time.
 *  
 *  If we determine that someone is walking up the stairs (i.e. the light
 *  was turned on because someone walked through the lower sensor, and they
 *  then walked through the upper sensor), we also send a message to the 
 *  porch light switch to turn the porch light on. We do not ever turn the 
 *  porch light off, because the light might have been turned on manually, 
 *  and we don't want to turn it off in that case.
 * 
 *  Note that if someone comes and drops off a package (walking through the 
 *  lower and then the upper sensor), this could leave the porch light on 
 *  until morning (when the smart switch is programmed to turn off the 
 *  light). But this could also help us notice that something has been 
 *  dropped off. 
 * 
 *  (We actually also have a third light, between the stairway light and the
 *  porch light. That light is controlled via its own integrated PIR sensor, 
 *  which operates entirely independently of the porch and stairway lights.)
 *  
 *  DYP-A02YYWM is a sealed waterproof ultrasonic ranging sensor that is 
 *  compatible with HC-SR04. It uses this wiring scheme:
 *    red    +5V
 *    black  ground
 *    yellow trigger
 *    white  echo
 *    
 *  Cat5e CMR used for connections:
 *    blue    trigL
 *    blue-w  echoL
 *    orng    trigU
 *    orng-w  echoU
 *    grn     light
 *    grn-w   +5v (if vin is not +5v)
 *    brwn    gnd
 *    brwn-w  vin
 *    
 *  Debug messages are sent to console, and less verbose log messages are 
 *  sent to a Google Form. To set up the form, create a Google Form with a 
 *  single short answer field. Type anything into the short answer field, 
 *  click "Get pre-filled link," click "Get Link," and then click "Copy 
 *  Link." From the copied link, the long string after "/d/e/" is the 
 *  formID, and the number after "entry." is the entryID, which should be 
 *  defined in the global constants, below.
 *  
 */
/*
 * Sonar Light Switch v3.0
 * * Optimized for: Arduino Uno WiFi Rev 2
 * * Created by: Michael Kwun
 * * NOTE: This version contains placeholders. Replace all "REPLACE_ME" 
 * strings with your actual network and Google Form credentials.
 */

#include <avr/wdt.h>
#include <WiFiNINA.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <TimeLib.h>
#include <Dusk2Dawn.h>

/* --- Precision Logging Registry --- */
struct LogEvent {
  unsigned long* timestamp;
  const char* msg;
};

unsigned long logTimeOn = 0, logTimeOff = 0, logTimeWiFi = 0, logTimePorchOn = 0, logTimeKasaFail = 0;

LogEvent events[] = {
  {&logTimeWiFi, "WiFi RECONNECTED"},
  {&logTimeOn, "Stair light ON"},
  {&logTimePorchOn, "Porch light ON"},
  {&logTimeOff, "Stair light OFF"},
  {&logTimeKasaFail, "Kasa Porch Timeout"}
};

const int numEvents = sizeof(events) / sizeof(events[0]);

/* --- Pins --- */
const int pLedRelay = 2, pLedLower = 4, pLedUpper = 7;
const int pLight = 3, pTrigL = 5, pEchoL = 6, pTrigU = 8, pEchoU = 9;

/* --- Network & Config (REPLACE WITH YOUR DATA) --- */
const char ssid[] = "YOUR_WIFI_SSID";
const char passwd[] = "YOUR_WIFI_PASSWORD";
const IPAddress porch_light_IP (192,168,1,100); // Your Kasa Switch Static IP
const int porch_light_port = 9999;
const char formID[] = "YOUR_GOOGLE_FORM_ID_STRING";
const char entryID[] = "YOUR_ENTRY_ID_NUMBER";

/* --- Timing --- */
const unsigned long ping_range = 24UL * 74 * 2;
const unsigned long light_length = 60000UL;      
const unsigned long wifi_try_time = 60000UL;     
const unsigned long heartbeatInterval = 1800000UL; 
const unsigned long ssl_keepalive_interval = 25000UL;
const unsigned long porch_duration = 300000UL;

/* --- Objects & State --- */
WiFiClient client;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", -28800); // Offset for PST (-8)
Dusk2Dawn sf(37.7749, -122.4194, -8); // SF Lat/Long

unsigned long body_time = 0, low_detect_time = 0, up_detect_time = 0;
unsigned long lastHeartbeat = -1800001, last_ssl_activity = 0;
unsigned long porch_auto_off_time = 0;

// Solar Cache
int cachedSunrise = 0, cachedSunset = 0;
unsigned long lastSunUpdate = -3600001;

void setup() {
  pinMode(pLight, OUTPUT); pinMode(pTrigL, OUTPUT); pinMode(pEchoL, INPUT);
  pinMode(pTrigU, OUTPUT); pinMode(pEchoU, INPUT);
  pinMode(pLedRelay, OUTPUT); pinMode(pLedLower, OUTPUT); pinMode(pLedUpper, OUTPUT);
  pinMode(LED_BUILTIN, OUTPUT);
  
  digitalWrite(pTrigL, LOW); digitalWrite(pTrigU, LOW); digitalWrite(pLight, LOW);

  Serial.begin(9600);
  Serial.println("V3.0: LET THERE BE LIGHT");
  
  WiFi.begin(ssid, passwd);
  timeClient.begin();
  wdt_enable(WDT_PERIOD_8KCLK_gc); 

  form_log("SYSTEM BOOT V3.0");
}

void loop() {
  wdt_reset(); 
  unsigned long the_time = millis();
  wifi_check();
  timeClient.update();

  int sensor = find_body();
  if (sensor) {
    if (sensor & 1) low_detect_time = the_time;
    if (sensor & 2) up_detect_time = the_time;
    
    if (isItDark()) {
      body_time = the_time;
      if (!digitalRead(pLight)) {
        digitalWrite(pLight, HIGH);
        logTimeOn = the_time ? the_time : 1; 
      } 
      else if ((sensor & 2) && (the_time - low_detect_time < 40000) && (the_time - low_detect_time > 4000)) {
        porch_on(); 
      }
    }
  } 

  if (digitalRead(pLight) && (the_time - body_time >= light_length)) {
    digitalWrite(pLight, LOW);
    logTimeOff = the_time ? the_time : 1;
  }

  if (porch_auto_off_time != 0 && (long)(the_time - porch_auto_off_time) >= 0) {
    porch_off();
  }
  
  if (the_time - low_detect_time > 3000 && the_time - up_detect_time > 3000) {
    bool loggedSomething = false;
    for (int i = 0; i < numEvents; i++) {
      if (*(events[i].timestamp) > 0) {
        char logBuf[50];
        sprintf(logBuf, "%s (%lus ago)", events[i].msg, (the_time - *(events[i].timestamp))/1000);
        if (form_log(logBuf)) *(events[i].timestamp) = 0;
        loggedSomething = true;
        break; 
      }
    }
    if (!loggedSomething) {
      if (the_time - last_ssl_activity > ssl_keepalive_interval) keepSSLWarm();
      else porchLightHeartbeat(the_time);
    }
  }

  updateStatusLEDs(the_time);
  delay(100); 
}

/* --- KASA LOGIC --- */

void sendKasaCommand(const char* msg) {
  client.write((int)0); client.write((int)0);
  client.write(highByte(strlen(msg))); client.write(lowByte(strlen(msg)));
  int key = 0xAB;
  for (int i = 0; msg[i] != '\0'; i++) client.write(key ^= msg[i]);
}

void porch_on() {
  if (WiFi.status() != WL_CONNECTED) return;
  client.setTimeout(1500);
  if (client.connect(porch_light_IP, porch_light_port)) {
    sendKasaCommand("{\"system\":{\"get_sysinfo\":{}}}");
    String resp = ""; unsigned long start = millis();
    while (client.connected() && (millis() - start < 800)) {
      if (client.available()) resp += (char)client.read();
    }
    client.stop();

    if (resp.indexOf("\"relay_state\":1") == -1) { 
      if (client.connect(porch_light_IP, porch_light_port)) {
        sendKasaCommand("{\"system\":{\"set_relay_state\":{\"state\":1}}}");
        client.stop();
        porch_auto_off_time = millis() + porch_duration;
        logTimePorchOn = millis() ? millis() : 1;
      }
    }
  } else { logTimeKasaFail = millis() ? millis() : 1; }
}

void porch_off() {
  if (WiFi.status() != WL_CONNECTED) { porch_auto_off_time = millis() + 10000; return; }
  client.setTimeout(1500);
  if (client.connect(porch_light_IP, porch_light_port)) {
    sendKasaCommand("{\"system\":{\"set_relay_state\":{\"state\":0}}}");
    client.stop();
    porch_auto_off_time = 0;
  } else { porch_auto_off_time = millis() + 60000; } 
}

/* --- NETWORK & UTILS --- */

void keepSSLWarm() {
  if (!client.connected()) {
    client.stop(); client.setTimeout(2000); wdt_reset();
    client.connectSSL("docs.google.com", 443);
  } else {
    client.println("X-KeepAlive: 1"); client.println();
  }
  last_ssl_activity = millis();
}

int form_log(char* msg) {
  if (WiFi.status() != WL_CONNECTED) return 0;
  if (!client.connected()) {
    client.stop(); client.setTimeout(2000); wdt_reset();
    if (!client.connectSSL("docs.google.com", 443)) return 0;
  }
  wdt_reset();
  client.print("GET /forms/d/e/"); client.print(formID);
  client.print("/formResponse?&submit=Submit?usp=pp_url&entry."); client.print(entryID); client.print("=");
  while ( *msg != '\0') {
    if (('a' <= *msg && *msg <= 'z') || ('A' <= *msg && *msg <= 'Z') || ('0' <= *msg && *msg <= '9') || *msg == '-' || *msg == '.' || *msg == '_' || *msg == '~') client.print(*msg);
    else { client.print("%"); if ( *msg < 16 ) client.print("0"); client.print(*msg,HEX); }
    msg++;
  }
  client.println(" HTTP/1.1"); client.println("Host: docs.google.com");
  client.println("Connection: keep-alive"); client.println();
  unsigned long t = millis();
  while (client.connected() && millis() - t < 500) { if (client.available()) client.read(); }
  last_ssl_activity = millis();
  return 1;
}

void porchLightHeartbeat(unsigned long t) {
  if (t - lastHeartbeat > heartbeatInterval) {
    lastHeartbeat = t; 
    if (WiFi.status() == WL_CONNECTED) {
      client.setTimeout(1000);
      if (client.connect(porch_light_IP, porch_light_port)) {
        sendKasaCommand("{\"system\":{\"get_sysinfo\":{}}}");
        client.stop();
      }
    }
  }
}

int wifi_check() {
  static bool was_connected = false; static unsigned long last_wifi_try = 0;
  if (WiFi.status() == WL_CONNECTED) {
    if (!was_connected) { logTimeWiFi = millis() ? millis() : 1; was_connected = true; }
    return 1; 
  }
  was_connected = false;
  if (millis() - last_wifi_try < wifi_try_time) return 0;
  last_wifi_try = millis(); WiFi.begin(ssid, passwd);
  return (WiFi.status() == WL_CONNECTED);
}

int find_body() {
  int body = 0; unsigned long dist;
  digitalWrite(pTrigL, HIGH); delayMicroseconds(10); digitalWrite(pTrigL, LOW);
  dist = pulseIn(pEchoL, HIGH, 25000);
  if (dist < ping_range && dist) body = 1;
  delay(2);
  digitalWrite(pTrigU, HIGH); delayMicroseconds(10); digitalWrite(pTrigU, LOW);
  dist = pulseIn(pEchoU, HIGH, 25000);
  if (dist < ping_range && dist) body |= 2;
  return body;
}

bool isItDark() {
  unsigned long the_time = millis();
  unsigned long epochTime = timeClient.getEpochTime();
  int currentMin = (timeClient.getHours() * 60) + timeClient.getMinutes();
  
  if (epochTime < 1735689600UL) return (currentMin >= 1020 || currentMin <= 480);

  if (the_time - lastSunUpdate > 3600000UL) {
    bool isDST = (month(epochTime) > 3 && month(epochTime) < 11);
    cachedSunrise = sf.sunrise(year(epochTime), month(epochTime), day(epochTime), isDST); 
    cachedSunset  = sf.sunset(year(epochTime), month(epochTime), day(epochTime), isDST);
    lastSunUpdate = the_time;
  }

  return (currentMin >= (cachedSunset - 15) || currentMin <= (cachedSunrise + 15));
}

void updateStatusLEDs(unsigned long t) {
  digitalWrite(pLedRelay, digitalRead(pLight));
  digitalWrite(pLedLower, (t - low_detect_time < 300000 && low_detect_time != 0));
  digitalWrite(pLedUpper, (t - up_detect_time < 300000 && up_detect_time != 0));
  digitalWrite(LED_BUILTIN, (WiFi.status() == WL_CONNECTED));
}
