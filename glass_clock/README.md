# Glass clock

A screensaver clock in the style of the iPhone's lock screen: tall, thin digits of frosted glass over a picture of your
choice from Home Assistant, with the date above them.

The digits are drawn from rounded strokes with a clear body and a bright edge, because a plugin cannot bring a font of
its own. The plugin calculates each digit into a small picture (one byte a pixel, in PSRAM) and shows it as one object,
so the edge is one bright line all round. It needs a board with PSRAM; without room for the pictures the clock stays empty
(the date and the background still show). Over a colourful, soft picture they look like glass; they are a close likeness, not real glass (a screen of
this kind cannot blur what lies behind a part).

## Set up

1. Add the plugin to a screen in Tessera. The screen builds once. It needs plugin API 0.17: the dev version of the
   Tessera app and firmware.
2. In the screen's Settings, open the **Screensaver** card and turn the screensaver on.
3. Add the step **Glass clock** and choose an image entity (or a camera) in Home Assistant for it. The picture of that
   entity is the background.
4. Put the step first in the order, so it shows before the other steps.
5. Set the options under the step: the layout, the digit style, how much the picture is dimmed, the date.

## Digit style

The option **Digit style** picks one of three looks for the digits: **Glass** (the default, as before), **Style 2** and
**Style 3**. Style 2 has heavier, shorter digits. Style 3 is prepared in
`glass_clock.cpp` (`draw_style3`, `STYLES[2]`) and draws like glass until its code is added.

## Overlay picture

The option Overlay address takes the web address of a PNG that lies over the clock (for example a file in Home Assistant's
`www` folder, which is served as `https://<your domain>/local/<file>.png`). Its transparency is kept; Overlay opacity
makes it fainter. The host of that address must be written in `permissions.network` of `tessera-plugin.yaml`
(replace the placeholder `your-domain.example` with your own domain, the host name only), or the overlay is blocked.

## Privacy

Only the overlay picture leaves the home network: the Tessera app fetches the address you enter, so that host sees
the request (the app's address and the time). Nothing else is sent. Without an overlay address nothing is fetched.

## Good to know

- The picture comes from the entity: change the image entity in Home Assistant (an automation that picks a new
  wallpaper, say) and the screensaver changes with it. A screen without picture memory (the CYD) shows the clock on black.
- Automatic layout stacks the hours over the minutes on a square or tall screen and puts the four digits in a row on a
  wide one.
- Nothing moves: the digits, the colon and the background picture stay on the same place (the manifest says `moves: true`,
  so the screen does not shift the saver a millimetre every minute against burn-in). Keep that in mind on OLED screens.
  A 12-hour screen shows AM or PM beside the date.
- It uses nothing outside the screen but the picture the app fetches from Home Assistant.


## Overlay in front of the clock (cut out)

The screen draws every picture as opaque pixels (RGB565, no alpha). Tessera's app puts your PNG over the background
before it sends it, so the PNG's transparent parts arrive as copies of the background. That is why a picture laid over
the clock hid it, and why its opacity only faded those copies over the clock, never "the transparent parts".

So the overlay lies under the clock, and the plugin cuts the digits out where the PNG is opaque. The cut-out (a
silhouette of the PNG) is made **automatically while the firmware is built**:

1. Put your overlay PNG into the screen's config folder (beside the screen's YAML file) and call it
   `glass_clock_overlay.png`. The build finds it by itself. (A copy in `components/glass_clock/` works too.)
   Put **the same picture** into Home Assistant's `www` folder and enter its address under the app option
   **Overlay address**: the screen shows that one, the silhouette is made from the one in step 1, so the two must match.
2. Build the screen again. The build log says `glass_clock: mask 240x240 (56 KB) from ... % opaque`. It names the file
   it used, so you can see whether it was the new one. Without the file the log says that nothing is cut out.
3. Set **Overlay position** to *Clock cut out by the overlay* (the default).

Another name or place: in `plugin.yaml` set `overlay_image` to a file name (looked for in the screen's config folder, then
in `components/glass_clock/`), a full path, or a web address the build can reach (for example
`https://<your domain>/local/overlay.png`).

Optional keys next to `overlay_image`: `glass_width` / `glass_height` (default 480 x 480, the Guition 4; the picture is
fitted to this size like on the screen: filling it, centred), `mask_width` (default 240; the flash it takes is
width x height bytes) and `mask_grow` (cut the digits out that many pixels wider than the picture).

The silhouette is part of the firmware, so build the screen again when the PNG changes. The colon is cut out like the digits. The date line is not cut out. To look at the
silhouette without building: `python components/glass_clock/mask_builder.py overlay.png` writes
`overlay_mask_preview.png` (white = cut out).
