# BensN2K

Small Arduino helper set for building NMEA2000/J1939 CAN frames, plus a simple usage sketch.

## Contents
- `arduino/N2KFrames.h`: Frame helper functions (PGN IDs + payload packing).
- `N2KFramesExample.ino`: Example sketch showing how to call the helpers.

## Notes
- Requires the `due_can` library and an Arduino Due (or compatible) setup.
- CAN speed is set to 250000 in the example sketch.
