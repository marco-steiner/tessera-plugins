# Changelog

## 0.4.0
- The colon no longer blinks (option Blinking colon removed). In Smooth mode it is a picture like the digits, so it lies on
  the same level as the time and the overlay cuts it out pixel by pixel, instead of the dots standing in front of the overlay.
- The overlay's silhouette is made automatically when the firmware is built: set `overlay_image` (web address or file) in
  plugin.yaml and the plugin reads the PNG's alpha channel itself (Pillow, which ESPHome brings along). overlay_mask.h and
  glass_clock_mask.html are no longer needed. New optional keys glass_width, glass_height, mask_width and mask_grow.
  The mask is now handed to the component with set_mask() instead of being a constant in a header.

## 0.3.0
- The overlay no longer hides the clock: it lies under the clock, and in the default position (Cut out) the digits are cut
  out wherever the PNG is opaque, from a silhouette in overlay_mask.h (made by glass_clock_mask.html). Behind and In front
  (the old way) remain as options. Why: the screen draws pictures as opaque RGB565, so a PNG's transparent parts arrive as
  copies of the background, and the opacity setting only faded those copies over the clock.

## 0.2.0
- New options Overlay address and Overlay opacity: a PNG from a web address (the host is named in permissions.network) lies over the clock.
- New option Position: moves the date and the time together up or down.

## 0.1.0
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
