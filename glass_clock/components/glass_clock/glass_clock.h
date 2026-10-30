#pragma once
#include <cstdint>
#include "esphome/core/component.h"
#include "esphome/components/smart_display/plugin_api.h"

namespace esphome::glass_clock {

// The plugin: an ESPHome component and a tessera::Plugin. It registers its screensaver in setup().
class GlassClock : public Component, public tessera::Plugin {
 public:
  void setup() override;
  float get_setup_priority() const override { return setup_priority::DATA; }
  // The overlay's silhouette (alpha of the PNG, width x height bytes, row by row), made when the firmware is built.
  void set_mask(const uint8_t *data, int width, int height);
};

}  // namespace esphome::glass_clock
