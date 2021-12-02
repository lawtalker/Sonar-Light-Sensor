//#define DEBUG
/*
 *  Sonar Light Switch
 * 
 *  Changelog:
 *  v2.0: 2021 Dec. 1; uses two range sensors to turn on light from either 
 *        direction, and switched to wifi Arduino to turn on porch light when 
 *        going up
 *  v1.1: 2021 Nov. 12; eliminate daylight sensor logic (relies on a smart 
 *        switch to turn power off during daylight)
 *  v1.0: 2013 Oct. 18; uses light sensor for daylight detection
 * 
 *  @date:    December 1, 2021
 *  @version: 2.0
 *  
 *  Copyright (c) 2013, 2021 Michael Kwun
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
 *  (HS-200). The stairway switch is programmed to turn off power to the 
 *  stairway light during the daytime. That also turns off power to the 
 *  Arduino, so motion during the daytime will not trigger the light. 
 *  
 *  When the Kasa switch for the stairway light is on, the light still will 
 *  not turn on unless a pin on the Arduino is held high, triggering a relay.
 *  That is, the stairway light is controlled by two switches in series,
 *  both of which must be on for the light to be on.
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
 *    grn     +5v (if vin is not +5v)
 *    grn-w   light
 *    brwn    gnd
 *    brwn-w  vin
 * 
 */

#include <SPI.h>
#include <WiFiNINA.h>

/* Arduino pins */
const int pLight =  3;           // controls relay for stairway light
const int pTrigL =  5;           // ping lower sensor
const int pEchoL =  6;           // echo lower sensor
const int pTrigU =  8;           // ping upper sensor
const int pEchoU =  9;           // echo upper sensor

/* network constants (IP address for porch light switch is hardcoded) */
const char ssid[] = "YourSSID";
const char passwd[] = "YourWiFiPassword";
const IPAddress porch_light_IP (192,168,1,100); // Kasa porch switch
const int porch_light_port = 9999;

/*  max ping time (in microseconds) that will trigger light 
 *  (74 microsconds per inch or 29 per cm, multiplied by 2 for roundtrip)
 *  actual distance will vary by an inch or so with ambient temperature
 */
const unsigned long ping_range = 24UL * 74 * 2;

/*  Time in milliseconds that stairway light should stay on without 
 *  detecting a body again. E.g. (90UL*1000) would mean 90 seconds
 */ 
const unsigned long light_length = 60UL * 1000;

/* Minimum time between attempts to connect to WiFi. */
const unsigned long wifi_try_time = 60UL * 1000; 

/* Minimum time between attempts to connect to Kasa switch. */
const unsigned long connect_try_time = 15UL * 60 * 1000; 

/* Plaintext JSON message to turn on the porch light. */
const char msg_on[] = "{\"system\":{\"set_relay_state\":{\"state\":1}}}";


/* initial setup at powerup */
void setup() {
  
  pinMode(pLight, OUTPUT);
  pinMode(pTrigL, OUTPUT);
  pinMode(pTrigU, OUTPUT);
  
  #ifdef DEBUG
  pinMode(LED_BUILTIN, OUTPUT);
  #endif
  
  /* these should already all be low, but let's be sure */
  digitalWrite(pTrigL, LOW);
  digitalWrite(pTrigU, LOW);
  digitalWrite(pLight, LOW);

  /* open console for debug */
  #ifdef DEBUG  
  Serial.begin(9600);
  while (!Serial) {
    ;
  }
  Serial.println();
  Serial.println();
  Serial.println("Let there be light!");
  Serial.println("Debug serial connection is open.");
  Serial.print("Stairway light: pin ");
  Serial.println(pLight, DEC);
  Serial.print("Light time (ms): ");
  Serial.println(light_length, DEC);
  Serial.print("Sensors triggered by body within (inches): ");
  Serial.println(ping_range/2/74, DEC); 
  #endif

  /* to ensure clean trigger for first ping */
  delay(10);
}

/* subroutine: ping two range detectors, return results in 1 & 2 bits */
int find_body() {
  
  int body = 0;
  unsigned long dist;

  /* send ping with lower sensor */
  digitalWrite(pTrigL, HIGH);
  delayMicroseconds(10);
  digitalWrite(pTrigL, LOW);

  /*  Wait for echo & set 1 bit if something is close enough. 
   *  
   *  The echo pulse should begin within about 250 microseconds, and the 
   *  HC-SR04 should timeout the echo pulse in less than 40,000 
   *  microseconds, so the timeout really should only be hit if there's 
   *  a hardware fault.
   *  
   *  In my use case, dist should always be non-zero, because the
   *  stairwell is less than 3 ft wide. So we could test only against
   *  ping_range. But we will test for dist being non-zero anyway,
   *  just in case we ever change the use case.
   */
  dist = pulseIn(pEchoL, HIGH, 40000);
  if (dist < ping_range && dist) {    
    body = 1;
  }
 
  /* repeat with upper sensor, set 2 bit if something is detected */
  digitalWrite(pTrigU, HIGH);
  delayMicroseconds(10);
  digitalWrite(pTrigU, LOW);
  dist = pulseIn(pEchoU, HIGH, 40000);
  if (dist < ping_range && dist) {
    body |= 2;
  }

  return body;
}

/* subroutine: send string to client using TSHP. */
void TSHP_write(WiFiClient &cl, char* msg) {
  
  /* send TSHP length header (used by TSHP for TCP but not UDP) */
  cl.write((int)0);
  cl.write((int)0);
  cl.write(highByte(strlen(msg)));
  cl.write(lowByte(strlen(msg)));

  /* send TSHP payload (with required weak obfuscation) */
  int key = 0xAB; // TSHP magic number
  for (int i = 0; msg[i] != '\0'; i++) {
    cl.write(key ^= msg[i]);
  }
}

/***************
 *             *
 *  MAIN LOOP  *
 *             *
 ***************/
void loop() {

  /* one-time initialization, variables survive looping */
  static unsigned long body_time; // last time a body was detected
  static int up_down;             // why stairway light was turned on
  static WiFiClient client;
  static unsigned long wifi_last_attempt;
  static bool startup = true;
  static bool connect_fail = false;
  static unsigned long connect_fail_time;

  /* initialized each time through the loop */
  unsigned long the_time = millis(); // rollover after ~50 days
  int wifi_status = WiFi.status();

  /****
   * 
   * Our approach is to loop repeatedly through this task list:
   *   (a) connect to WiFi if needed;
   *   (b) check our two range sensors;
   *     (1) if we found something, then
   *       - if light off, turn on; 
   *       - if light on & someone is going up, turn on porch light;
   *     (2) if found nothing, then see if we should turn light off;
   *   (c) pause to limit looping to ten times per second.
   * 
   ****/

  /*  Connect to wifi if necessary, but Wifi.begin() is blocking, so if 
   *  attempt fails, don't try again for a while except that we'll allow 
   *  multiple attempts right after powering up. (A successful connection can 
   *  complete in under 3 seconds, as does a failure due to a missing AP. A 
   *  failure due an incorrect password seems to take around 5 seconds.)
   */
  if (startup && the_time > wifi_try_time) {
    startup = false;
  }
  if (wifi_status != WL_CONNECTED) {    
    if (startup || the_time - wifi_last_attempt > wifi_try_time) {   
      
      wifi_last_attempt = the_time;
      
      #ifdef DEBUG
      Serial.print("Connecting to ");
      Serial.print(ssid);
      Serial.print(" WiFi AP because status is: ");
      switch (wifi_status) {
        case WL_IDLE_STATUS:
          Serial.println("IDLE.");
          break;
        case WL_CONNECT_FAILED:
          Serial.println("CONNECT_FAILED.");
          break;
        case WL_CONNECTION_LOST:
          Serial.println("CONNECTION_LOST.");
          break;
        case WL_DISCONNECTED:
          Serial.println("DISCONNECTED.");
          break;
        default:
          Serial.print("code ");
          Serial.print(wifi_status,DEC);
          Serial.println(".");
          break;
      }
      #endif
      
      wifi_status = WiFi.begin(ssid, passwd);
      
      #ifdef DEBUG
      Serial.print("WiFi.begin() exited with status ");
      switch (wifi_status) {
        case WL_IDLE_STATUS:
          Serial.println("IDLE.");
          break;
        case WL_CONNECTED:
          Serial.println("CONNECTED.");
          break;
        case WL_CONNECT_FAILED:
          Serial.println("CONNECT_FAILED.");
          break;
        default:
          Serial.print("code ");
          Serial.print(wifi_status,DEC);
          Serial.println(".");
          break;
      }
      #endif
    }
  } /* end wifi connection block */ 
    
  /* check range sensors and process results */
  if ( int sensor = find_body() ) {
    
    body_time = the_time;
    
    #ifdef DEBUG
    switch(sensor) {
      case 1:
        Serial.println("Lower sensor triggered.");
        break;
      case 2:
        Serial.println("Upper sensor triggered.");
        break;
      case 3:
        Serial.println("Both sensors triggered.");
        break;    
    }
    #endif

    /* Body found & light off: turn on light & store trigger */
    if (! digitalRead(pLight)) {
 
      digitalWrite(pLight, HIGH);
      up_down = sensor;

      #ifdef DEBUG
      digitalWrite(LED_BUILTIN, HIGH);
      Serial.println("Turned on stairway light.");
      #endif

    } /* end body found & light off */

    /*  Body found & light on: 
     *  
     *  If light was triggered by just the bottom sensor
     *  AND upper sensor is now triggered
     *  AND we have a wifi connection
     *  THEN ask Kasa to turn on the porch light (but only once).
     */
    else if (up_down == 1 && sensor & 2 && wifi_status == WL_CONNECTED) {

      #ifdef DEBUG
      Serial.println("Someone coming up! ");
      #endif

      /*  connect() is blocking, and a failed connection seems to take 
       *   ~30 seconds. If we've had a recent failed connection, don't try 
       *   again for a while.
       */
      if (!connect_fail) {
        #ifdef DEBUG
        Serial.println("Trying to connect to Kasa light switch.");
        #endif
        if (client.connect(porch_light_IP, porch_light_port)) {
          TSHP_write(client, msg_on);
          up_down = 0;  // turn on porch light only once
     
          #ifdef DEBUG
          Serial.println("Told Kasa light switch to turn on porch light.");
          #endif
        }
        else {
          connect_fail = true;
          connect_fail_time = the_time;
          #ifdef DEBUG
          Serial.println("Connection to Kasa light switch failed.");
          Serial.print("Won't try again for ");
          Serial.print(connect_try_time, DEC);
          Serial.println(" ms.");
          #endif
        }
      }
      #ifdef DEBUG
      else {
        Serial.println("Skipping Kasa due to recent failed connection.");
      }
      #endif /* end Kasa message handling */     
        
    } /* end body found & light on */
  } /* end body found*/

  /*  
   *  No body found:
   *  
   *  If stairway light is on
   *  AND last body detected was at least light_length ago
   *  THEN turn off stairway light.
   */
  else if (digitalRead(pLight) && the_time - body_time > light_length) {
    
    digitalWrite(pLight, LOW);

    #ifdef DEBUG
    digitalWrite(LED_BUILTIN, LOW);
    Serial.print("No body detected for ");
    Serial.print(light_length, DEC);
    Serial.println(" ms, so turned stairway light off.");
    #endif
  } /* end no body found */

  /* clear connection fail flag if appropriate */
  if (connect_fail && the_time - connect_fail_time > connect_try_time) {
    connect_fail = false;
  }

  /* delay so that we loop no more than 10 times per second */
  while (millis() - the_time < 100) {
    ;
  }
} /* end loop() */
