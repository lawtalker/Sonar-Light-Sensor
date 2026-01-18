/*
 * Sonar Light Switch
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
 *  If we determine that someone is walking up the stairs (i.e. someone 
 *  walked through the lower sensor, and they then walked through the upper 
 *  sensor), we also send a message to the porch light switch to turn the 
 *  porch light on (if it's not already on), and turn it off after a while.
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
 *  defined in the global constants.
 *  
 */
