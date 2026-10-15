MedBox - Wi-Fi Medicine Dose Reminder & Dispenser (Proof of Concept)
=====================================================================

A low-cost proof of concept that reminds you when a medicine dose is due and
dispenses it on confirmation. The schedule is entered from a web page on your
phone, served directly by the board over Wi-Fi. No cloud account or app
install is needed.

DISCLAIMER: This is a hobby / learning project. It is NOT a medical device and
must not be relied on for real medication management.


FEATURES
--------
- Add and delete dose times (name + time, up to 8) from a phone browser
- Schedule saved in flash memory, so it survives power loss
- Time synced over the internet (NTP), set for India (UTC+5:30)
- At dose time the LED and buzzer pulse until you press the button
- Pressing the button swings the servo to release one dose, then beeps once
- Doses not confirmed within 45 seconds are logged as missed (Serial Monitor)
- "Test alert now" button on the page to test the hardware any time
- JSON endpoint (GET /api/schedule) for a future native app


HARDWARE
--------
- UNO+WiFi R3 board (ATmega328P + ESP8266 + CH340G USB), the ESP8266 runs
  this project; the ATmega328P is not used
- Servo motor (SG90 or similar)
- LED + 220 ohm resistor
- Active buzzer (3.3V-compatible; for a 5V buzzer use an NPN transistor such
  as 2N2222/BC547 with a 1k ohm base resistor)
- Push button
- Breadboard and jumper wires
- External 5V supply for the servo (4xAA pack, charger or power bank)
- Optional: 470 uF capacitor across the servo supply if the board resets


WIRING
------
Use the 4x3 "ESP Pin's" header on the board. Pin labels are printed on the
back of the board.

  Part                   Connect to
  ---------------------  ------------------------------------------------
  Servo signal           GPIO12
  Servo power (red)      External 5V +
  Servo ground           External 5V - AND board GND
  LED + (long leg)       GPIO2, through the 220 ohm resistor
  LED - (short leg)      GND
  Buzzer +               GPIO16
  Buzzer -               GND
  Button                 GPIO4 on one leg, GND on the other (no resistor;
                         the code uses the internal pull-up)

Notes:
- All grounds must be common (board GND + external supply negative + parts).
- Never power the servo from the 3.3V pin.
- Wire with USB unplugged.
- GPIO2 is a boot pin. If Wi-Fi stops connecting after wiring the LED,
  disconnect the LED wire and retest.
- Do not connect parts to RST, CH_PD, VCC or TOUT.
- See medbox_circuit_diagram.png and medbox_wiring_guide.xlsx for the diagram
  and checklists.


SOFTWARE SETUP (Arduino IDE 2.x)
--------------------------------
1. Install Arduino IDE from arduino.cc.
2. File > Preferences > Additional boards manager URLs, add:
   http://arduino.esp8266.com/stable/package_esp8266com_index.json
3. Boards Manager > search "esp8266" > install "esp8266 by ESP8266 Community".
   (The Servo and EEPROM libraries are included; nothing else to install.)
4. Open medbox_esp8266.ino.
5. Edit the Wi-Fi details near the top of the file:
     const char* WIFI_SSID = "YOUR_WIFI_NAME";
     const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";
   Use a 2.4 GHz network (the ESP8266 does not support 5 GHz).
   Do NOT commit your real Wi-Fi password to GitHub.
6. Tools settings:
     Board:         NodeMCU 1.0 (ESP-12E Module)
     Upload Speed:  115200
     Port:          the CH340 serial port of your board
   Do not select "Arduino Uno" - that targets the wrong chip.


DIP SWITCHES (UNO+WiFi R3 board)
--------------------------------
The 8 DIP switches route USB to either chip. Settings below are the common
ones; confirm against the table printed on your board.
- Upload to ESP8266:        switches 5, 6, 7 ON, all others OFF
- Serial Monitor (ESP8266): switches 5, 6 ON, 7 OFF
Set the switches, then upload. After uploading, change them for the Serial
Monitor and press the board's reset button.


FIRST RUN
---------
1. Upload the sketch, then open Serial Monitor at 115200 baud.
2. Press reset. The board prints its IP address, e.g. 192.168.1.50.
   (Some garbage text at boot is normal for the ESP8266.)
3. On a phone connected to the same Wi-Fi, open http://<that IP> in a browser,
   or try http://medbox.local (may not work on some Android phones).
4. Add a dose (medicine name + time) and tap Add.
5. Tap "Test alert now" to check LED, buzzer and button/servo.


USING IT
--------
- At a scheduled time the LED blinks and the buzzer pulses.
- Press the button to dispense: the servo opens (90 degrees), closes, and the
  buzzer beeps once.
- If nobody responds within 45 seconds the dose is logged as missed.
- Adjust SERVO_OPEN / SERVO_CLOSED and ALERT_TIMEOUT_MS in the sketch to suit
  your mechanism.


WEB ENDPOINTS
-------------
  GET  /               Schedule page
  POST /add            Add a dose (form fields: name, time as HH:MM)
  POST /delete         Delete a dose (field: i = dose index)
  POST /test           Trigger a test alert
  GET  /api/schedule   Current schedule and device time as JSON

Example response:
  {"time":"08:15:30","doses":[{"id":0,"name":"Vitamin D","time":"08:30"}]}


TROUBLESHOOTING
---------------
- "fatal error: WiFi.h: No such file": wrong sketch for the board; this
  project uses ESP8266WiFi.h with the NodeMCU 1.0 board selected.
- No serial port: install the CH340 driver and use a data USB cable.
- Upload stuck at "Connecting": recheck the DIP switches.
- Garbage in Serial Monitor: set baud to 115200.
- "Wi-Fi failed" repeating: check name/password and use 2.4 GHz.
- Page won't load: phone must be on the same Wi-Fi (not mobile data); use the
  IP address instead of medbox.local.
- Servo twitches or the board resets: power problem. Check the common ground
  and add a 470 uF capacitor across the servo supply.
- Wi-Fi stops working after wiring the LED: GPIO2 is a boot pin; disconnect
  the LED wire and retest.
- Wrong time: time needs internet access at boot to sync; adjust
  GMT_OFFSET_SEC in the sketch for other time zones.


LIMITATIONS
-----------
- The page has no password; keep the device on your home network.
- Time is only synced over NTP, so the device needs Wi-Fi at boot.
- Dispensing blocks the loop for about 2 seconds.
- Dose times match the exact minute; if an alert is still active when another
  dose becomes due, that dose is skipped.
- Single compartment only.


IDEAS FOR NEXT STEPS
--------------------
- Native phone app (Flutter / React Native) using the JSON endpoint
- JSON POST endpoint to add doses from an app
- Multiple compartments (servo angles per slot)
- RTC module (DS3231) for operation without Wi-Fi at boot
- Caregiver notifications and remote access (e.g. MQTT)
- Password or token protection for the web page
- Log taken/missed doses


FILES
-----
  medbox_esp8266.ino          Arduino sketch
  medbox_circuit_diagram.png  Wiring diagram
  medbox_wiring_guide.xlsx    Wiring table, parts list, steps and test plan
  README.txt                  This file


LICENSE
-------
Add your preferred license here (e.g. MIT).
