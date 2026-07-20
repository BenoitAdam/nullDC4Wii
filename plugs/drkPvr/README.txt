FPS overlay port for nullDC4Wii v0.55

This was ported from the working v0.54 FPS implementation into a clean v0.55 source tree.

Files changed:
- main.cpp
  Added the SHOW FPS menu option and getter.

- SPG.cpp
  Calculates and formats FPS and speed values.

- gxRend.cpp
  Draws the FPS text directly onto the Wii XFB.
  Includes support for the v0.55 async render path.

Test result:
The project builds successfully and the FPS overlay works on Wii hardware.

The overlay can be enabled or disabled through the SHOW FPS menu option.