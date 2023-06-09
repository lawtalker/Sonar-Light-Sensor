/*
 *  Sonar Light Switch
 * 
 *  Changelog:
 *  v2.2: 2023 June 8: improve logging and clean up code
 *  v2.1: 2023 June 7; add logging to Google Sheet and clean up code
 *  v2.0: 2021 Dec. 1; use two range sensors to turn on light from either 
 *        direction, and switch to wifi Arduino to turn on porch light when 
 *        going up
 *  v1.1: 2021 Nov. 12; eliminate daylight sensor logic (relies on a smart 
 *        switch to turn power off during daylight)
 *  v1.0: 2013 Oct. 18; use light sensor for daylight detection
 * 
 *  @date:    June 8, 2023
 *  @version: 2.2
 *  
 *  Copyright (c) 2013, 2021, 2023 Michael Kwun
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

#include <SPI.h>
#include <WiFiNINA.h>

/*  We use an external smart switch that cuts power at sunrise, and switches
 *  power back on at sunset, which means millis() will never overflow  */

/* Arduino pins */
const int pLight =  3;           // controls relay for stairway
const int pTrigL =  5;           // ping lower sensor
const int pEchoL =  6;           // echo lower sensor
const int pTrigU =  8;           // ping upper sensor
const int pEchoU =  9;           // echo upper sensor

/* network constants (IP address for porch light switch is hardcoded) */
const char ssid[] = "YourSSID";
const char passwd[] = "YourWiFiPassword";
const IPAddress porch_light_IP (192,168,1,100); // Kasa porch switch
const int porch_light_port = 9999;

/* IDs for Google Form for logging */
const char formID[] =
  "very-very-long-string-for-google-form-ID";
const char entryID[] = "123456789";

/* generic client for WiFiNINA connections */
WiFiClient client;

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
  pinMode(LED_BUILTIN, OUTPUT);
  
  /* these should already all be low, but let's be sure */
  digitalWrite(pTrigL, LOW);
  digitalWrite(pTrigU, LOW);
  digitalWrite(pLight, LOW);

  /* open console for debug */
  Serial.begin(9600);
  while (!Serial) {
    ;
  }
  Serial.println("LET THERE BE LIGHT!");
  Serial.print("Stairway light: pin ");
  Serial.println(pLight, DEC);
  Serial.print("Light time (ms): ");
  Serial.println(light_length, DEC);
  Serial.print("Sensors triggered by body within (inches): ");
  Serial.println(ping_range/2/74, DEC); 

  /* to ensure clean trigger for first ping */
  delay(10);
}

/* subroutine: ping two range detectors
   return results in 1 & 2 bits */
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

/* subroutine: remote logging via a Google Form. 
 * sends msgs to console and, if possible, a Google Form
 * skips Form if no wifi or recent failed connection
 * 
 * returns 1 if Form connection succeeded, and 0 if not 
 */
int form_log(char* msg) {

  static unsigned long down_time = 0;

  /* send message to console */
  Serial.println(msg);

  /* don't log to Google Form if no wifi */
  if (WiFi.status() != WL_CONNECTED) {
    return 0;
  }
  
  /* connectSSL() is blocking, and a failed connection will block for ~30
     seconds, so don't try if we've had a failure recently. */
  if (down_time) {
    if (millis() < down_time + connect_try_time) {
      return 0; 
    }
    down_time = 0;
  }

  /* if no recent connection failure, open an https connection */
  if (client.connectSSL("docs.google.com", 443)) {

    /* beginning of GET for Google Form */
    client.print("GET /forms/d/e/");
    client.print(formID);
    client.print("/formResponse?&submit=Submit?usp=pp_url&entry.");
    client.print(entryID);
    client.print("=");

    /* percent encode the log entry, one char at a time */
    while ( *msg != '\0') {
      /* passthrough unreserved chars per RFC 3986 sec. 2.3 
         ALPHA / DIGIT / "-" / "." / "_" / "~"               */
      if (    ('a' <= *msg && *msg <= 'z') 
           || ('A' <= *msg && *msg <= 'Z') 
           || ('0' <= *msg && *msg <= '9') 
           || *msg == '-' || *msg == '.' 
           || *msg == '_' || *msg == '~'   ) {
        client.print(*msg);
      } 

      /* percent encode everything else per RFC 3986 sec. 2.1 */ 
      else {
        client.print("%");
        if ( *msg < 16 ) {
          client.print("0"); // add leading zero if necessary
        }
        client.print(*msg,HEX);
      }
      msg++; // next character in the log entry
    }

    client.println(" HTTP/1.1"); // end of GET
    
    client.println("Host: docs.google.com");
    client.println("Connection: close");
    client.println();

    client.stop();

    return 1;   // logged entry
  }

  /* https connection failed */
  down_time = millis();
  Serial.println("Connection to Google Form failed.");
  return 0;
  
}

/* subroutine: tell porch light to turn on using TSHP. 
   returns 1 if successful */
int porch_on() {

  static unsigned long down_time = 0;

  /* connect() is blocking, and a failed connection will block for ~30
     seconds, so don't try if we've had a failure recently. */
  if (down_time) {
    if (millis() < down_time + connect_try_time) {
      return 0; 
    }
    down_time = 0;
  }

  /* if no recent connection failure, try to send the Kasa switch an
     "on" message via TCP and TSHP */ 
  if (client.connect(porch_light_IP, porch_light_port)) {

    /* send TSHP length header (used by TSHP for TCP but not UDP) */
    client.write((int)0);
    client.write((int)0);
    client.write(highByte(strlen(msg_on)));
    client.write(lowByte(strlen(msg_on)));
    
    /* send TSHP payload (with required weak obfuscation) */
    int key = 0xAB; // TSHP magic number
    for (int i = 0; msg_on[i] != '\0'; i++) {
      client.write(key ^= msg_on[i]);
    }
    client.stop();
    
    form_log("Asked Kasa to turn on porch light.");
    
    return 1; // connection succeeded
  }
  
  /* Kasa connection failed */
  down_time = millis();
  Serial.println("Connection to Kasa light switch failed.");

  return 0;
}

/* subroutine: check wifi and connect if needed
 * won't try if failed recently, except multiple tries are allowed at startup
 * 
 * returns 1 if connected
 */
int wifi_check() {
  static unsigned long wifi_last_attempt = -wifi_try_time;
    
  unsigned long the_time = millis();
  int wifi_status = WiFi.status();

  /* if we're connected, all is well */
  if (wifi_status == WL_CONNECTED) {
    return 1; 
  }

  /* if we had a failed attempt recently and it's not shortly after
     shortly after power up, don't try again, because WiFi.begin()
     is blocking and takes 3-5 seconds */
  if (the_time < wifi_last_attempt + wifi_try_time) {
    return 0;
  }

  /* update last attempt time unless recently powered up */
  if (the_time > wifi_try_time) {
    wifi_last_attempt = the_time;
  }

  /* report connection attempt to console */
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

  /* actual connection attempt */
  if (WiFi.begin(ssid, passwd) == WL_CONNECTED) {
    form_log("WiFi CONNECTED.");
    return 1;
  }
  Serial.println("WiFi did not connect.");
  return 0;

}

/***************
 *             *
 *  MAIN LOOP  *
 *             *
 ***************/
void loop() {

  /* one-time initialization, variables survive looping 
     initializing to -light_length basically means "a long time ago" */
  static unsigned long 
    body_time = -light_length,  // last time body detected
    low_time = -light_length,   // last time body detected at low sensor
    ping_time = 60UL*60*1000;   // one hour from start up
  static int up_down;           // sensor that triggered light
  
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

  /* Check wifi, and attempt to connect if needed */
  int wifi_status = wifi_check();  

  /* log proof of life every hour */
  unsigned long the_time = millis(); 
  if (the_time > ping_time) {
    form_log("Hourly ping.");
    ping_time += 60UL*60*1000;
  }

  /* check range sensors and process results */  
  if ( int sensor = find_body() ) {
    
    body_time = the_time;

    if (sensor & 1) {
      Serial.println("Lower sensor triggered.");    
      low_time = the_time;  
    }
    if (sensor & 2) {
      Serial.println("Upper sensor triggered.");  
    }

    /* Body found & light off: turn on light */
    if (! digitalRead(pLight)) {
 
      digitalWrite(pLight, HIGH);
      digitalWrite(LED_BUILTIN, HIGH);

      up_down = sensor;

      form_log("Stairway light on");
 
    } /* end body found & light off */

    /* body found & light on */
    /* turn on porch light if sosmeone moving from lower to upper and we have wifi */
    else if (sensor & 2 
          && up_down == 1 
          && the_time < low_time + light_length 
          && wifi_status ) {
      porch_on();
    } /* end body found & light on */

  } /* end body found*/

  /*  
   *  No body found:
   *  
   *  If stairway light is on
   *  AND last body detected was at least light_length ago
   *  THEN turn off stairway light.
   */
  else if (digitalRead(pLight) && the_time > body_time + light_length) {
    digitalWrite(pLight, LOW);
    digitalWrite(LED_BUILTIN, LOW);
    form_log("Stairway light off.");
  } /* end no body found */

  /* delay so that we loop no more than 10 times per second */
  while (millis() < the_time + 100) {
    ;
  }
} /* end loop() */
