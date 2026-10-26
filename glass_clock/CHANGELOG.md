# Changelog

## 0.1.0
- New option Detail (light, full, minimal): fewer layers per arc mean fewer LVGL objects and less memory. Light is the default.
- Free ends of the round strokes get their own cap with a bright border (the 3, 2, 5, 6, 9); each stroke says in `open`
  which of its ends are free.
- First version: a screensaver step of its own that shows the time in tall, thin digits of frosted glass over the
  picture of an image entity (or a camera) of Home Assistant, with the date above. Layout, how much the picture is
  dimmed, the date and a blinking colon are options. Needs a screen with plugin API 0.17: Tessera's dev app and firmware.
