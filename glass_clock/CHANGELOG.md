# Changelog

## 0.1.0
- New options Overlay picture and Overlay opacity: the picture of the step's entity with the suffix _overlay is shown over the clock.
- The smooth mode has its own, much taller and thinner digits with oval bowls (the 9 and 6 like the 0); Strokes keeps the
  first shape. Auto layout picks the arrangement that makes the digits larger.
- New option Drawing: Smooth (default) draws each digit as one soft A8 picture with a bright edge all round, a handful of
  objects instead of about sixty; Strokes keeps the first way, built from arcs and bars.
- New option Detail (light, full, minimal): fewer layers per arc mean fewer LVGL objects and less memory. Light is the default.
- Free ends of the round strokes get their own cap with a bright border (the 3, 2, 5, 6, 9); each stroke says in `open`
  which of its ends are free.
- First version: a screensaver step of its own that shows the time in tall, thin digits of frosted glass over the
  picture of an image entity (or a camera) of Home Assistant, with the date above. Layout, how much the picture is
  dimmed, the date and a blinking colon are options. Needs a screen with plugin API 0.17: Tessera's dev app and firmware.
