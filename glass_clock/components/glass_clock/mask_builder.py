"""Builds the overlay's silhouette (the mask the clock is cut out with) from the overlay PNG, at build time.

Only Pillow is needed, which ESPHome brings along. This file knows nothing about ESPHome, so it can be tried on its own:
    python mask_builder.py overlay.png [glass_w glass_h mask_w grow]
which writes overlay_mask_preview.png (white = the clock is cut out here).
"""
import io
import urllib.request

MAX_BYTES = 10 * 1024 * 1024
TIMEOUT = 20


def read_source(source, search_dirs):
    """Return the bytes of the overlay picture. `source` is a web address or a file name; a file is looked for in each
    of search_dirs in turn (the screen's config folder, then this component's folder)."""
    if source.lower().startswith(("http://", "https://")):
        request = urllib.request.Request(source, headers={"User-Agent": "esphome-glass-clock"})
        with urllib.request.urlopen(request, timeout=TIMEOUT) as response:
            data = response.read(MAX_BYTES + 1)
        if len(data) > MAX_BYTES:
            raise ValueError(f"{source} is larger than {MAX_BYTES // 1024 // 1024} MB")
        return data
    import os

    tried = []
    for folder in [None, *search_dirs]:
        path = source if folder is None else os.path.join(folder, source)
        tried.append(path)
        if os.path.isfile(path):
            with open(path, "rb") as f:
                return f.read()
    raise FileNotFoundError("overlay picture not found, tried: " + ", ".join(tried))


def build_mask(data, glass_w, glass_h, mask_w, grow=0):
    """Return (mask_w, mask_h, bytes, stats): the alpha channel of the picture, laid over a glass of glass_w x glass_h
    pixels the way the screen lays a picture over it (scaled until it fills the glass, centred, the rest cut off),
    reduced to mask_w pixels across. `grow` (glass pixels) spreads the opaque parts outwards, in a round shape.
    stats = (share of opaque pixels 0..1, whether the picture had an alpha channel at all)."""
    from PIL import Image, ImageChops

    img = Image.open(io.BytesIO(data))
    has_alpha = "A" in img.getbands() or "transparency" in img.info
    alpha = img.convert("RGBA").getchannel("A")

    mask_h = max(1, round(mask_w * glass_h / glass_w))
    iw, ih = alpha.size
    scale = max(mask_w / iw, mask_h / ih)
    dw, dh = max(mask_w, round(iw * scale)), max(mask_h, round(ih * scale))
    # BOX averages the area under each mask pixel when shrinking (what the browser's smoothing did in the old tool).
    method = Image.Resampling.BOX if scale < 1 else Image.Resampling.BILINEAR
    alpha = alpha.resize((dw, dh), method)
    left, top = (dw - mask_w) // 2, (dh - mask_h) // 2
    alpha = alpha.crop((left, top, left + mask_w, top + mask_h))

    n = round(grow * mask_w / glass_w) if grow > 0 else 0
    if grow > 0:
        n = max(1, n)
    if n > 0:
        out = alpha.copy()
        for dy in range(-n, n + 1):
            for dx in range(-n, n + 1):
                if (dx or dy) and dx * dx + dy * dy <= n * n:
                    shifted = Image.new("L", alpha.size, 0)
                    shifted.paste(alpha, (dx, dy))
                    out = ImageChops.lighter(out, shifted)
        alpha = out

    raw = alpha.tobytes()
    opaque = sum(1 for v in raw if v > 127) / len(raw)
    return mask_w, mask_h, raw, (opaque, has_alpha)


if __name__ == "__main__":
    import sys
    from PIL import Image

    args = sys.argv[1:]
    if not args:
        sys.exit(__doc__)
    gw, gh, mw, grow = (int(a) for a in (args[1:5] + ["480", "480", "240", "0"][len(args[1:5]):]))
    with open(args[0], "rb") as f:
        w, h, raw, (share, has_alpha) = build_mask(f.read(), gw, gh, mw, grow)
    Image.frombytes("L", (w, h), raw).save("overlay_mask_preview.png")
    print(f"{w}x{h}, {share:.0%} opaque, alpha channel: {has_alpha}")
