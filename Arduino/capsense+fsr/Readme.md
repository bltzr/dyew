*capsense+fsr* project
======================

What it is
----------

This was the main arduino's project inside *das Körperrauchen* project.
This program runs on micro's boards and scan both capacitive and resistive sensors.
It has been updated for *Dyew* project by Djeff Regottaz with LED control.
The sensor values are sent through serial interface over USB in OSC slip-encoded packets.
Many parameters of the boards can be set through OSC too.

Building / Uploading
--------------------
Since there are 5 boards to update on each build, `upload_all.sh` is a simple script that kindly do that for you.
 
