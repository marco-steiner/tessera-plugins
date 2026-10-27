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
