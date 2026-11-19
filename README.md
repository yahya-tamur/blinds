electronic device to open and close blinds (using a standard 4.5mm bead chain)
automatically.

I have not tested this extensively, consider this as notes after creating one
working model.

This repository includes 3d designs for an enclosure, pcb design, firmware,
and an android app. The enclosure was designed without AI.

The blinds can go up and down using the switch, an ir remote, wifi interface, or alarms. The alarms can be set based on the sunrise and sunset.


Assembly
=====

PCB
---
   * This uses a separate ESP32 dev kit as well as a TMC2226 stepstick module that are soldered onto the board, on their own pins.
   * Design files and BOM is in the repository. A slightly different ESP32 and stepstick module may work, but may require firmware updates.
   * This is hand-solderable, and there is a hand soldering guide in this repository's [github pages](https://yahya-tamur.github.io/blinds/soldering-guide-v0.2.0/).

Additional items required
------
 * Nema 17 stepper motor
    * A motor of any length is fine, though my blinds are pretty heavy, and work best with a 60mm motor.
    * Different motors may require a current adjustment in the firmare
 * 3 mm bolts of various sizes and hex nuts
 * 3 x 10 mm metal rods
 * one bearing same size as 693zz
 * Power adapter: 24V 2 amp 5.5 × 2.5 mm barrel plug, center positive.
 * six 5 x 2 mm magnets, attached in alternating polarity to the sprocket with super glue
 * A 5mm [flange coupler](https://www.amazon.com/dp/B08334MFVT) This is put on the motor shaft, flat face towards the outside.
   Four small bolts are bolted on to the holes, bolf heads facing out. These interface with the sun gear of 'planetary'. If you
   use a different flange coupler, you may have to edit that interface of the sun gear.

3d printing the enclosure
-------

* I don't know if this physically fits any other blinds, you may need to edit 'enclosure'.
* The 'planetary' (make sure to separate stl by parts before slicing), 'carrier2', and 'sprocket' are designed to be printed
  in PA6-GF25 or similar 'engineering filament'.
  * Use grease on these parts.
  * The planetary can skip gears without 'carrier2' and 'planetary' installed. There are dots for alignment.
* Any other part can be printed with PLA pro.
* Auto orient the parts, and hand paint supports in a few obvious spots.


firmware
------
  * Set the wifi SSID and passord in `wifi.properties`. The end and beginning of the movement needs to be programmed in the wifi interface.
  * Afterwards, it also works with an IR remote (set up using wifi) and the switch.
  * To get the browser's location to setup sunrise / sunset alarms, it will access this repository's github pages to request
    permission. This is because only https sites can do so. The actual location isn't sent, it's just used in the browser to set in the device.

android app
----
  * apk available in releases
  * The android app makes sure it's connected to the same wifi, then accesses the wifi interface.
  * It can also set alarms in the blinds and the android clock app at the same time.
