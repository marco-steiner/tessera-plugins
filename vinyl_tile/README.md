# Vinyl tile

The music that plays as a record in its sleeve, as a tile that fills the whole 4x4 screen. The cover of what plays is
the sleeve, with the record peeking out of it, and the title and the artist sit beside or under it. Four round keys
mute the player, turn its volume down, play or pause it, and turn its volume up.

It looks like the Vinyl deck screensaver, but it is a tile you place in Layout, so it is on the screen all day and not
only in standby.

## Set up

1. Add the plugin to a screen in Tessera. The screen builds once.
2. In Layout, place the **Vinyl tile** from the library's Plugins group, at 4x4.
3. In the inspector, choose the media player the tile belongs to.
4. Turn **Keys** off for a tile that only shows the music.

## Good to know

- It shows on screens that show pictures. The CYD, the Waveshare 3.5-inch and the Hosyond 4-inch show a plain tile
  without the cover.
- A key shows only when the player has it in Home Assistant: a player without a volume has the play key alone.
- The first tap on a volume key while the player is muted takes the mute off and changes nothing else.
- The screen must be allowed to perform actions in Home Assistant (it is when the app added it).
