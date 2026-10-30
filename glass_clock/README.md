# Glass clock

A screensaver clock in the style of the iPhone's lock screen: tall, thin digits of frosted glass over a picture of your
choice from Home Assistant, with the date above them.

The digits are drawn from rounded strokes with a clear body and a bright edge, because a plugin cannot bring a font of
its own. Over a colourful, soft picture they look like glass; they are a close likeness, not real glass (a screen of
this kind cannot blur what lies behind a part).

## Set up

1. Add the plugin to a screen in Tessera. The screen builds once. It needs plugin API 0.17: the dev version of the
   Tessera app and firmware.
2. In the screen's Settings, open the **Screensaver** card and turn the screensaver on.
3. Add the step **Glass clock** and choose an image entity (or a camera) in Home Assistant for it. The picture of that
   entity is the background.
4. Put the step first in the order, so it shows before the other steps.
5. Set the options under the step: the layout, how much the picture is dimmed, the date, the blinking colon.

## Two ways to draw the digits

- **Smooth** (default): the plugin calculates each digit into a small picture (one byte a pixel, in PSRAM) and shows it
  as one object. The edge of the digit is one bright line all round, with no brighter spots where strokes meet. It needs a
  board with PSRAM; without room for the pictures it uses Strokes by itself.
- **Strokes**: every digit is built from LVGL arcs and bars, about sixty objects. Use it if Smooth does not suit your
  screen. The option Detail sets how many layers an arc has (Light, Full or Minimal).

## Overlay picture

The option Overlay address takes the web address of a PNG that lies over the clock (for example a file in Home Assistant's
`www` folder, which is served as `https://<your domain>/local/<file>.png`). Its transparency is kept; Overlay opacity
makes it fainter. The host must be written in `permissions.network` of `tessera-plugin.yaml`.

## Privacy

Only the overlay picture leaves the home network: the Tessera app fetches the address you enter, so that host sees
the request (the app's address and the time). Nothing else is sent. Without an overlay address nothing is fetched.

## Good to know

- The picture comes from the entity: change the image entity in Home Assistant (an automation that picks a new
  wallpaper, say) and the screensaver changes with it. A screen without picture memory (the CYD) shows the clock on black.
- Automatic layout stacks the hours over the minutes on a square or tall screen and puts the four digits in a row on a
  wide one.
- The digits stand on the same place for hours, so the screen moves the whole saver a millimetre every minute against
  burn-in. A 12-hour screen shows AM or PM beside the date.
- It uses nothing outside the screen but the picture the app fetches from Home Assistant.


## Overlay in front of the clock (cut out)

The screen draws every picture as opaque pixels (RGB565, no alpha). Tessera's app puts your PNG over the background
before it sends it, so the PNG's transparent parts arrive as copies of the background. That is why a picture laid over
the clock hid it, and why its opacity only faded those copies over the clock, never "the transparent parts".

So the overlay lies under the clock, and the plugin cuts the digits out where the PNG is opaque. The cut-out (a
silhouette of the PNG) is made **automatically while the firmware is built**:

1. In `plugin.yaml` set `overlay_image` to your overlay PNG: a web address (for example
   `https://<your domain>/local/overlay.png`) or a file name (in the screen's config folder or in
   `components/glass_clock/`). Use the same picture as the app option **Overlay address**.
2. Build the screen again. The build log says `glass_clock: mask 240x240 (56 KB) from ... % opaque`.
3. Set **Overlay position** to *Clock cut out by the overlay* (the default).

Optional keys next to `overlay_image`: `glass_width` / `glass_height` (default 480 x 480, the Guition 4; the picture is
fitted to this size like on the screen: filling it, centred), `mask_width` (default 240; the flash it takes is
width x height bytes) and `mask_grow` (cut the digits out that many pixels wider than the picture).

The silhouette is part of the firmware, so build the screen again when the PNG changes. It works with the smooth drawing
(not Strokes), and a colon dot that lies on the silhouette stays hidden. The date line is not cut out. To look at the
silhouette without building: `python components/glass_clock/mask_builder.py overlay.png` writes
`overlay_mask_preview.png` (white = cut out).
