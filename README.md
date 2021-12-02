#Sonar Light Switch
 
I use this sketch for an outdoor light in the middle of a stairway. 
There's also a light on a porch at the top of a second, higher run of 
stairs. Both lights are connected to TP-Link Kasa smart switches 
(HS-200). The stairway switch is programmed to turn off power to the 
stairway light during the daytime. That also turns off power to the 
Arduino, so motion during the daytime will not trigger the light. 

When the Kasa switch for the stairway light is on, the light still will 
not turn on unless a pin on the Arduino is held high, triggering a relay.
That is, the stairway light is controlled by two switches in series,
both of which must be on for the light to be on.

The sketch is designed for use with an Arduino Uno WiFi rev 2, so we have 
a network interface. We also use two ultrasonic ranging sensors (HC-SR04 
clones) that detect bodies below and above the stairway light. A body at 
either sensor will trigger the stairway light. The light stays on until 
no body is detected at either sensor for a set period of time.

If we determine that someone is walking up the stairs (i.e. the light
was turned on because someone walked through the lower sensor, and they
then walked through the upper sensor), we also send a message to the 
porch light switch to turn the porch light on. We do not ever turn the 
porch light off, because the light might have been turned on manually, 
and we don't want to turn it off in that case.
 
Note that if someone comes and drops off a package (walking through the 
lower and then the upper sensor), this could leave the porch light on 
until morning (when the smart switch is programmed to turn off the 
light). But this could also help us notice that something has been 
dropped off. 
 
(We actually also have a third light, between the stairway light and the
porch light. That light is controlled via its own integrated PIR sensor, 
which operates entirely independently of the porch and stairway lights.)

DYP-A02YYWM is a sealed waterproof ultrasonic ranging sensor that is 
compatible with HC-SR04. It uses this wiring scheme:
  red    +5V
  black  ground
  yellow trigger
  white  echo
  
Cat5e CMR used for connections:
  blue    trigL
  blue-w  echoL
  orng    trigU
  orng-w  echoU
  grn     +5v (if vin is not +5v)
  grn-w   light
  brwn    gnd
  brwn-w  vin
