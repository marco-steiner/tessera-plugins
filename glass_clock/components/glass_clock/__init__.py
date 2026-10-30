"""Glass clock: the ESPHome side. smart_display.register_plugin() reads the id, the version, whether its screensaver
moves itself and the screen's texts from the manifest and translations/ beside this folder (docs/FIRMWARE_API.md).

The one thing this file does besides that: if `overlay_image` is set, it makes the overlay's silhouette (the mask the
digits are cut out with) from that PNG while the firmware is built, and compiles it in. Nothing to generate by hand."""
import logging
import os

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import smart_display
from esphome.const import CONF_ID
from esphome.core import CORE

from . import mask_builder

_LOGGER = logging.getLogger(__name__)

# The core component every Tessera screen has; the plugin API lives in it.
DEPENDENCIES = ["smart_display"]

CONF_OVERLAY_IMAGE = "overlay_image"
CONF_GLASS_WIDTH = "glass_width"
CONF_GLASS_HEIGHT = "glass_height"
CONF_MASK_WIDTH = "mask_width"
CONF_MASK_GROW = "mask_grow"
CONF_MASK_ID = "mask_id"

glass_clock_ns = cg.esphome_ns.namespace("glass_clock")
GlassClock = glass_clock_ns.class_("GlassClock", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(GlassClock),
        cv.GenerateID(CONF_MASK_ID): cv.declare_id(cg.uint8),
        # The overlay PNG: a web address (https://<your domain>/local/overlay.png) or a file name (looked for in the
        # screen's config folder, then in this component's folder). Without it there is no silhouette.
        cv.Optional(CONF_OVERLAY_IMAGE): cv.string,
        # The glass the picture is fitted to (filling it, centred), in pixels: 480 x 480 for the Guition 4.
        cv.Optional(CONF_GLASS_WIDTH, default=480): cv.int_range(min=100, max=2000),
        cv.Optional(CONF_GLASS_HEIGHT, default=480): cv.int_range(min=100, max=2000),
        # How many pixels wide the silhouette is (it sits in the flash: 240 x 240 are 56 KB).
        cv.Optional(CONF_MASK_WIDTH, default=240): cv.int_range(min=60, max=480),
        # Cut the digits out this many glass pixels wider than the picture.
        cv.Optional(CONF_MASK_GROW, default=0): cv.int_range(min=0, max=20),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await smart_display.register_plugin(var, __file__)

    source = config.get(CONF_OVERLAY_IMAGE)
    if not source:
        _LOGGER.info("glass_clock: no overlay_image, the clock is not cut out by an overlay")
        return

    try:
        data = mask_builder.read_source(
            source, [str(CORE.config_dir), os.path.dirname(os.path.abspath(__file__))]
        )
        width, height, raw, (opaque, has_alpha) = mask_builder.build_mask(
            data,
            config[CONF_GLASS_WIDTH],
            config[CONF_GLASS_HEIGHT],
            config[CONF_MASK_WIDTH],
            config[CONF_MASK_GROW],
        )
    except Exception as err:  # a clear message instead of a traceback in the middle of the build
        raise cv.Invalid(f"glass_clock: the overlay picture '{source}' could not be used for the mask: {err}") from err

    if not has_alpha:
        _LOGGER.warning(
            "glass_clock: %s has no transparency, so the whole glass is cut out and the clock stays invisible", source
        )
    elif opaque < 0.001:
        _LOGGER.warning("glass_clock: %s is (almost) fully transparent, nothing will be cut out", source)
    _LOGGER.info("glass_clock: mask %dx%d (%d KB) from %s, %.0f%% opaque", width, height, len(raw) // 1024, source, opaque * 100)

    cg.add(var.set_mask(cg.progmem_array(config[CONF_MASK_ID], list(raw)), width, height))
