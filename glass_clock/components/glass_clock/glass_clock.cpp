#include "glass_clock.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include "esphome/core/log.h"

namespace esphome::glass_clock {

using tessera::Font;
namespace ui = tessera::ui;

// A step of the screensaver of its own (plugin API 0.17): the time in tall, thin digits of frosted glass over the
// picture of an image entity, as the iPhone's lock screen draws it.
//
// A plugin has no font of its own, so each digit is calculated from rounded strokes (bars and arcs) into one soft A8 picture
// in PSRAM. Everything is made once in show(); a new minute only draws the new digits into their pictures. The manifest
// says `moves: true`, so the screen does not shift the saver: every part stays exactly where it is drawn.
namespace {

static const char *const TAG = "glass_clock";

// The shape of a digit, in units of its width (1 wide, H tall, a stroke T thick). A digit is a set of segments: a bar runs
// from (a, b) to (c, d) along its centre line; an arc is the part of a circle around (a, b) from the angle c to the angle d,
// clockwise, in degrees as LVGL counts them (0 east, 90 south). A bowl is two arcs and two bars, so it can be as long as it
// needs to be; the two bowls of the 8 touch in the middle. EXT is how far the straight sides of a bowl run, UP2 and LOW2
// the centres of the arcs that close the upper and the lower bowl.
enum Kind : uint8_t { BAR, ARC };
struct Seg {
  Kind kind;
  float a, b, c, d;
};
struct Glyph {
  const Seg *segs;
  uint8_t count;
};

namespace glass {
constexpr float H = 3.0f, T = 0.15f, R = (1.0f - T) / 2;
constexpr float LEFT = T / 2, RIGHT = 1.0f - T / 2, TOP = T / 2, BOTTOM = H - T / 2, MID = 0.5f;
constexpr float UPPER = T / 2 + R, LOWER = H - T / 2 - R;
constexpr float EXT = H / 2 + T / 2 - 1.0f;
constexpr float UP2 = UPPER + EXT, LOW2 = LOWER - EXT;

constexpr Seg G0[] = {{ARC, MID, UPPER, 180, 360},
                      {ARC, MID, LOWER, 0, 180},
                      {BAR, LEFT, UPPER, LEFT, LOWER},
                      {BAR, RIGHT, UPPER, RIGHT, LOWER}};
constexpr Seg G1[] = {{BAR, 0.72f, TOP, 0.72f, BOTTOM},
                      {BAR, 0.16f, 0.42f, 0.72f, TOP}};
constexpr Seg G2[] = {{ARC, MID, UPPER, 180, 360},
                      {BAR, RIGHT, UPPER, RIGHT, UP2},
                      {ARC, MID, UP2, 0, 25},
                      {BAR, 0.894f, UP2+0.184f, LEFT, BOTTOM},
                      {BAR, LEFT, BOTTOM, RIGHT, BOTTOM}};
constexpr Seg G3[] = {{ARC, MID, UPPER, 200, 360},
                      {BAR, RIGHT, UPPER, RIGHT, UP2},
                      {ARC, MID, UP2, 0, 90},
                      {BAR, 0.30f, H/2, MID, H/2},
                      {ARC, MID, LOW2, 270, 360},
                      {BAR, RIGHT, LOW2, RIGHT, LOWER},
                      {ARC, MID, LOWER, 0, 160}};
constexpr Seg G4[] = {{BAR, 0.74f, TOP, 0.74f, BOTTOM},
                      {BAR, 0.74f, TOP, LEFT, 0.70f*H},
                      {BAR, LEFT, 0.70f*H, RIGHT, 0.70f*H}};
constexpr Seg G5[] = {{BAR, 0.144f, TOP, 0.144f, 1.46f},
                      {BAR, 0.144f, TOP, RIGHT, TOP},
                      {ARC, MID, 1.7f, 215, 360},
                      {BAR, RIGHT, 1.7f, RIGHT, LOWER},
                      {ARC, MID, LOWER, 0, 155}};
constexpr Seg G6[] = {{ARC, MID, 1.7f, 180, 360},
                      {ARC, MID, LOWER, 0, 180},
                      {BAR, LEFT, LOW2, LEFT, LOWER},
                      {BAR, RIGHT, 1.7f, RIGHT, LOWER},
                      {BAR, LEFT, UPPER, LEFT, LOW2},
                      {ARC, MID, UPPER, 180, 335}};
constexpr Seg G7[] = {{BAR, LEFT, TOP, RIGHT, TOP},
                      {BAR, RIGHT, TOP, 0.28f, BOTTOM}};
constexpr Seg G8[] = {{ARC, MID, UPPER, 180, 360},
                      {ARC, MID, UP2, 0, 180},
                      {BAR, LEFT, UPPER, LEFT, UP2},
                      {BAR, RIGHT, UPPER, RIGHT, UP2},
                      {ARC, MID, LOW2, 180, 360},
                      {ARC, MID, LOWER, 0, 180},
                      {BAR, LEFT, LOW2, LEFT, LOWER},
                      {BAR, RIGHT, LOW2, RIGHT, LOWER}};
constexpr Seg G9[] = {{ARC, MID, UPPER, 180, 360},
                      {ARC, MID, 1.3f, 0, 180},
                      {BAR, LEFT, 0.5f, LEFT, 1.3f},
                      {BAR, RIGHT, UPPER, RIGHT, UP2},
                      {BAR, RIGHT, UP2, RIGHT, LOWER},
                      {ARC, MID, LOWER, 0, 155}};
constexpr Glyph GLYPHS[10] = {{G0, 4}, {G1, 2}, {G2, 5}, {G3, 7}, {G4, 3}, {G5, 5}, {G6, 6}, {G7, 2}, {G8, 8}, {G9, 6}};
}  // namespace glass

// Style 2: heavier digits, 2 tall.
namespace bold {
constexpr float H = 2.0f, T = 0.28f, R = (1.0f - T) / 2;
constexpr float LEFT = T / 2, RIGHT = 1.0f - T / 2, TOP = T / 2, BOTTOM = H - T / 2, MID = 0.5f;
constexpr float UPPER = T / 2 + R, LOWER = H - T / 2 - R;
constexpr float EXT = H / 2 + T / 2 - 1.0f;
constexpr float UP2 = UPPER + EXT, LOW2 = LOWER - EXT;

constexpr Seg G0[] = {{ARC, MID, UPPER, 180, 360},
                      {ARC, MID, LOWER, 0, 180},
                      {BAR, LEFT, UPPER, LEFT, LOWER},
                      {BAR, RIGHT, UPPER, RIGHT, LOWER}};
constexpr Seg G1[] = {{BAR, 0.72f, TOP, 0.72f, BOTTOM},
                      {BAR, 0.16f, 0.42f, 0.72f, TOP}};
constexpr Seg G2[] = {{ARC, MID, UPPER, 180, 360},
                      {BAR, RIGHT, UPPER, RIGHT, UP2},
                      {ARC, MID, UP2, 0, 25},
                      {BAR, 0.810f, UP2+0.180f, 0.14f, 1.87f},
                      {BAR, LEFT, BOTTOM, RIGHT, BOTTOM}};
constexpr Seg G3[] = {{ARC, MID, UPPER, 200, 360},
                      {BAR, RIGHT, UPPER, RIGHT, UP2},
                      {ARC, MID, UP2, 0, 90},
                      {BAR, 0.30f, H/2, MID, H/2},
                      {ARC, MID, LOW2, 270, 360},
                      {BAR, RIGHT, LOW2, RIGHT, LOWER},
                      {ARC, MID, LOWER, 0, 160}};
constexpr Seg G4[] = {{BAR, 0.74f, TOP, 0.74f, BOTTOM},
                      {BAR, 0.74f, TOP, LEFT, 0.65f*H},
                      {BAR, LEFT, 0.65f*H, RIGHT, 0.65f*H}};
constexpr Seg G5[] = {{BAR, 0.144f, TOP, 0.144f, 1.0f},
                      {BAR, 0.144f, TOP, RIGHT, TOP},
                      {ARC, MID, 1.15f, 205, 360},
                      {BAR, RIGHT, 1.5f, RIGHT, 1.2f},
                      {ARC, MID, LOWER, 0, 155}};
constexpr Seg G6[] = {{ARC, MID, 1.20f, 180, 360},
                      {ARC, MID, LOWER, 0, 180},
                      {BAR, LEFT, LOW2, LEFT, LOWER},
                      {BAR, RIGHT, 1.2f, RIGHT, LOWER},
                      {BAR, LEFT, UPPER, LEFT, LOW2},
                      {ARC, MID, UPPER, 180, 335}};
constexpr Seg G7[] = {{BAR, LEFT, TOP, RIGHT, TOP},
                      {BAR, RIGHT, TOP, 0.28f, BOTTOM}};
constexpr Seg G8[] = {{ARC, MID, UPPER, 180, 360},
                      {ARC, MID, UP2, 0, 180},
                      {BAR, LEFT, UPPER, LEFT, UP2},
                      {BAR, RIGHT, UPPER, RIGHT, UP2},
                      {ARC, MID, LOW2, 180, 360},
                      {ARC, MID, LOWER, 0, 180},
                      {BAR, LEFT, LOW2, LEFT, LOWER},
                      {BAR, RIGHT, LOW2, RIGHT, LOWER}};
constexpr Seg G9[] = {{ARC, MID, UPPER, 180, 360},
                      {ARC, MID, 0.8f, 0, 180},
                      {BAR, LEFT, 0.5f, LEFT, 0.8f},
                      {BAR, RIGHT, UPPER, RIGHT, UP2},
                      {BAR, RIGHT, UP2, RIGHT, LOWER},
                      {ARC, MID, LOWER, 0, 155}};
constexpr Glyph GLYPHS[10] = {{G0, 4}, {G1, 2}, {G2, 5}, {G3, 7}, {G4, 3}, {G5, 5}, {G6, 6}, {G7, 2}, {G8, 8}, {G9, 6}};
}  // namespace bold

// What differs between the styles: the digits' shape and the gaps (in units of a digit's width: between the digits of a
// pair, around the colon, between the two rows of the stacked layout).
struct StyleDef {
  const Glyph *glyphs;
  float h, t, in, colon, rows;
};
constexpr StyleDef STYLES[3] = {
    {glass::GLYPHS, glass::H, glass::T, 0.15f, 0.45f, 0.30f},  // glass (the default)
    {bold::GLYPHS, bold::H, bold::T, 0.09f, 0.56f, 0.30f},     // style 2
    {glass::GLYPHS, glass::H, glass::T, 0.15f, 0.45f, 0.30f},  // style 3: placeholder, like glass
};

// What one digit slot holds: where it stands and which digit it shows.
struct Digit {
  char shown = 0;    // '0' to '9', ' ' for none, 0 before the first time
  int x = 0, y = 0;  // top left, in the glass's pixels
};

// The pictures of the digits (A8, one byte a pixel: how much of the ink shows), one per digit slot. They are kept in
// PSRAM for as long as the firmware runs and reused by the next saver, so nothing that LVGL still holds is ever freed.
constexpr int SLOTS = 4;
constexpr int PAD = 2;  // pixels around a digit's box, for the soft edge
constexpr int COLON_SLOT = SLOTS;  // the colon's picture lies after the four digits' pictures in the pool
struct RasterPool {
  uint8_t *pixels[SLOTS + 1] = {};
  size_t capacity[SLOTS + 1] = {};
  lv_image_dsc_t picture[SLOTS + 1] = {};
};
RasterPool &raster_pool() {
  static RasterPool pool;
  return pool;
}

// The overlay's silhouette: its alpha channel (255 where the PNG is opaque, 0 where it is transparent), as the glass shows
// it (the picture covers the whole glass), width x height bytes, row by row. Made from the PNG by __init__.py while the
// firmware is built and handed over by GlassClock::set_mask(). Empty (0 x 0) when the screen has no overlay_image.
struct MaskData {
  const uint8_t *data = nullptr;
  int w = 0, h = 0;
};
MaskData &mask_data() {
  static MaskData mask;
  return mask;
}

}  // namespace

class GlassSaver : public tessera::Saver {
 public:
  void show(const tessera::SaverContext &c) override {
    // An option the person never touched may not be in the word: read each with its default.
    const char *layout = c.options["layout"] | "auto";
    const int dim = std::max(0, std::min(80, (int) (c.options["dim"] | 35)));
    // -100 = Block ganz oben, 0 = mittig, +100 = ganz unten
    const int offset = std::max(-100, std::min(100, (int) (c.options["offset"] | 0)));
    const bool date = c.options["date"] | true;
    // The second picture, over the clock: the web address of a PNG on the host named in the manifest's
    // permissions.network (Home Assistant's www folder: https://<domain>/local/<file>.png), and how opaque it is as a whole.
    overlay_url_ = c.options["overlay_url"] | "";
    const bool overlay_on = !overlay_url_.empty();
    const int overlay_opa = std::max(10, std::min(100, (int) (c.options["overlay_opa"] | 100)));
    // Where the overlay lies. The screen draws a picture as opaque pixels (RGB565, no alpha): the app has already put the
    // PNG over what lies under it, so its transparent parts are copies of the background, and the picture can never let
    // the clock show through (and `overlay_opa` made those copies fade, which is what looked like a changed transparency).
    //   cut     under the clock, and the digits are cut out wherever the PNG is opaque (its silhouette, from
    //           the mask made from overlay_image at build time): the overlay looks as if it stood in front of the clock. The default.
    //   behind  under the clock, nothing cut out.
    //   front   over the clock as one opaque picture (the clock is hidden under it).
    const char *layer = c.options["overlay_layer"] | "cut";
    const bool overlay_front = std::strcmp(layer, "front") == 0;
    use_mask_ = overlay_on && std::strcmp(layer, "cut") == 0 && mask_data().data != nullptr && mask_data().w > 0 && mask_data().h > 0;
    glass_w_ = c.width;
    glass_h_ = c.height;
    // The look of the digits: "glass" (the default), "style2" or "style3".
    const char *digit_style = c.options["digit_style"] | "glass";
    style_ = std::strcmp(digit_style, "style2") == 0 ? STYLE_2 : (std::strcmp(digit_style, "style3") == 0 ? STYLE_3 : STYLE_GLASS);
    // The glass gets a little fainter at night, when the level is low and a face may go darker still.
    body_opa_ = c.night ? (lv_opa_t) 50 : (lv_opa_t) 77;
    rim_opa_ = c.night ? (lv_opa_t) 127 : (lv_opa_t) 179;

    // The picture of the chosen entity over the whole glass, and a shade over it, both under everything else. A board
    // that shows no pictures (the CYD) has black behind the glass.
    if (ui::pictures()) {
      place_ = ui::picture(c.parent, tessera::Placeholder::NONE);
      lv_obj_set_size(place_, c.width, c.height);
      lv_obj_set_pos(place_, 0, 0);
    }
    // The overlay: a picture of its own, from the address in the option. Made before the shade and the clock, so it is dimmed
    // with the background and the digits lie over it; `front` makes it last instead.
    auto make_overlay = [&]() {
      if (!ui::pictures() || !overlay_on) return;
      overlay_ = ui::picture(c.parent, tessera::Placeholder::NONE);
      lv_obj_remove_flag(overlay_, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_set_size(overlay_, c.width, c.height);
      lv_obj_set_pos(overlay_, 0, 0);
      lv_obj_set_style_opa(overlay_, (lv_opa_t) (overlay_opa * 255 / 100), 0);
      ESP_LOGI(TAG, "overlay picture made (%dx%d, opa %d%%, layer %s, mask %s)", (int) c.width, (int) c.height, overlay_opa,
               layer, use_mask_ ? "on" : "off");
    };
    if (!overlay_front) make_overlay();
    shade_ = ui::block(c.parent, theme::CAMERA_PAGE);
    lv_obj_set_style_radius(shade_, 0, 0);
    lv_obj_set_style_bg_opa(shade_, (lv_opa_t) (dim * 255 / 100), 0);
    lv_obj_set_size(shade_, c.width, c.height);
    lv_obj_set_pos(shade_, 0, 0);

    // The layout: four digits in a row, or two rows of two. `arrange` fixes the metrics, the size and the places of the
    // digits; with `auto` the arrangement that makes the digits larger wins.
    const StyleDef &sd = STYLES[style_];
    stroke_ = sd.t;
    glyph_h_ = sd.h;
    radius_ = (1.0f - sd.t) / 2;
    glyphs_ = sd.glyphs;
    const float IN = sd.in, COLON = sd.colon, ROWS = sd.rows;
    const int date_h = date ? ui::line_height(Font::TITLE) : 0;
    const int date_gap = date ? ui::px(10) : 0;
    bool stacked = false;
    int top = 0;
    auto arrange = [&]() {
      const float room = (float) (c.height - date_h - date_gap);
      const float side = std::min(c.width * 0.92f / (4 + 2 * IN + COLON), room * 0.62f / glyph_h_);
      const float stack = std::min(c.width * 0.84f / (2 + IN), room * 0.86f / (2 * glyph_h_ + ROWS));
      stacked = std::strcmp(layout, "stacked") == 0 || (std::strcmp(layout, "side") != 0 && stack > side);
      unit_ = stacked ? stack : side;
      rim_ = std::max(1, (int) std::lround(0.022f * unit_));
      const int digits_h = (int) std::lround((stacked ? 2 * glyph_h_ + ROWS : glyph_h_) * unit_);
      const int block_h = date_h + date_gap + digits_h;
      const int free_px = std::max(0, c.height - block_h);
      top = free_px / 2 + (int) std::lround(offset / 100.0f * free_px / 2) + date_h + date_gap;
      if (stacked) {
        const int x0 = (c.width - (int) std::lround((2 + IN) * unit_)) / 2;
        const int step = (int) std::lround((1 + IN) * unit_);
        const int row = (int) std::lround((glyph_h_ + ROWS) * unit_);
        digits_[0].x = digits_[2].x = x0;
        digits_[1].x = digits_[3].x = x0 + step;
        digits_[0].y = digits_[1].y = top;
        digits_[2].y = digits_[3].y = top + row;
      } else {
        // Built outwards from the colon, which stands exactly in the middle of the screen: the two pairs of digits are
        // mirror images of each other, and a digit slot never moves (a blank hour digit, as on 12 hours, keeps its place).
        const int mid = c.width / 2;
        const int dw = (int) std::lround(unit_), half_gap = (int) std::lround(COLON * unit_ / 2);
        const int in = (int) std::lround((1 + IN) * unit_);
        digits_[1].x = mid - half_gap - dw;
        digits_[2].x = mid + half_gap;
        digits_[0].x = digits_[1].x - in;
        digits_[3].x = digits_[2].x + in;
        for (Digit &d : digits_) d.y = top;
      }
    };
    arrange();
    // The pictures live in PSRAM. Without room for them there are no digits (the date and the background still show).
    raster_ = reserve_rasters();
    if (!raster_) ESP_LOGW(TAG, "no room for the digit pictures (PSRAM), the clock stays empty");

    if (date) {
      date_ = ui::label(c.parent, Font::TITLE, theme::INK_SOFT);
      lv_obj_set_width(date_, c.width);
      lv_obj_set_style_text_align(date_, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_set_pos(date_, 0, top - date_gap - date_h);
    }
    // The colon: two glass dots between the pairs, at a third and two thirds of the digits' height.
    const int dot = std::max(4, (int) std::lround(0.16f * unit_));
    const int cx = c.width / 2;  // the colon: exactly the middle of the screen
    if (raster_) {
      create_images(c.parent);
      // The colon is a picture like the digits, cut out by the overlay's silhouette pixel by pixel.
      if (!stacked) create_colon(c.parent, cx, dot);
    }

    if (overlay_front) make_overlay();
    if (!overlay_)
      ESP_LOGI(TAG, "no overlay: address %s, pictures() %s", overlay_on ? "set" : "empty", ui::pictures() ? "yes" : "no");
  }

  // The step's data: its entity's id, `entity`, which the picture's address is made of. A picture comes from the app,
  // by that address, exactly as it does for Tessera's own tiles.
  void on_state(JsonObjectConst data) override {
    // The second picture: the web address from the option, set the same way as the first picture's.
    ESP_LOGI(TAG, "on_state: overlay_ %s, place_ %s", overlay_ ? "set" : "null", place_ ? "set" : "null");
    if (overlay_ && !overlay_url_.empty()) {
      ESP_LOGI(TAG, "overlay address %s", overlay_url_.c_str());
      ui::set_picture(overlay_, overlay_url_, tessera::Fit::COVER);
    }
    if (!place_) return;
    const char *entity = data["entity"] | "";
    std::string address;
    if (*entity) {
      const bool still = std::strncmp(entity, "image.", 6) == 0 || std::strncmp(entity, "camera.", 7) == 0;
      address = std::string(still ? "camera:" : "entity:") + entity;
    }
    ui::set_picture(place_, address, tessera::Fit::COVER);
  }

  void on_tick(uint32_t epoch) override {
    if (!epoch) return;  // the screen's clock is not set yet
    if (epoch / 60 == minute_) return;
    minute_ = epoch / 60;
    // The time as the screen writes it ("14:05", or "2:05 PM" on 12 hours): the hours before the colon, two digits
    // after it, and what follows them.
    const std::string clock = tessera::clock_text(epoch);
    const size_t colon = clock.find(':');
    if (colon == std::string::npos || colon == 0 || colon + 2 >= clock.size()) return;
    const std::string hours = clock.substr(0, colon);
    put(0, hours.size() > 1 ? hours[hours.size() - 2] : ' ');
    put(1, hours.back());
    put(2, clock[colon + 1]);
    put(3, clock[colon + 2]);
    if (date_) {
      const size_t space = clock.find(' ', colon);
      const std::string suffix = space == std::string::npos ? "" : " " + clock.substr(space + 1);
      ui::set_text(date_, tessera::date_text(epoch) + suffix);
    }
  }

 private:
  // ---- The digits ----

  void put(int slot, char ch) {
    if (raster_) raster_digit(slot, ch);
  }

  // Room for the pictures of the digit slots, in PSRAM. False when it is not there.
  bool reserve_rasters() {
    RasterPool &pool = raster_pool();
    const size_t bytes = (size_t) picture_w() * picture_h();
    for (int i = 0; i <= SLOTS; ++i) {  // the four digits and the colon (its picture is narrower, so the same size fits)
      if (pool.capacity[i] >= bytes) continue;
      void *p = pool.pixels[i] ? roomy::reallocate(pool.pixels[i], bytes) : roomy::allocate(bytes);
      if (!p) return false;
      pool.pixels[i] = (uint8_t *) p;
      pool.capacity[i] = bytes;
    }
    return true;
  }
  int picture_w() const { return (int) std::lround(unit_) + 2 * PAD; }
  int picture_h() const { return (int) std::lround(glyph_h_ * unit_) + 2 * PAD; }

  // One lv_image for each digit slot, over the pictures reserved above.
  void create_images(lv_obj_t *parent) {
    RasterPool &pool = raster_pool();
    const int w = picture_w(), h = picture_h();
    const size_t bytes = (size_t) w * h;
    for (int i = 0; i < SLOTS; ++i) {
      std::memset(pool.pixels[i], 0, bytes);
      lv_image_dsc_t &dsc = pool.picture[i];
      dsc = {};
      dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
      dsc.header.cf = LV_COLOR_FORMAT_A8;
      dsc.header.w = w;
      dsc.header.h = h;
      dsc.header.stride = w;
      dsc.data = pool.pixels[i];
      dsc.data_size = bytes;
      lv_obj_t *img = lv_image_create(parent);
      lv_obj_remove_flag(img, LV_OBJ_FLAG_CLICKABLE);
      lv_image_set_src(img, &dsc);
      // An A8 picture has only the alpha: LVGL paints it in the recolour, the ink's colour (white on the dark glass).
      lv_obj_set_style_image_recolor(img, ui::color(theme::INK), 0);
      lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);
      lv_obj_set_pos(img, digits_[i].x - PAD, digits_[i].y - PAD);
      images_[i] = img;
    }
  }

  // The colon: one narrow A8 picture with the two dots, drawn once. Same glass look as the digits
  // (clear body, a little brighter at the top, bright edge) and cut out by the overlay's silhouette like them, so it
  // lies on the same level as the time.
  void create_colon(lv_obj_t *parent, int cx, int dot) {
    RasterPool &pool = raster_pool();
    const int w = dot + 2 * PAD, h = picture_h();
    const int x0 = cx - w / 2, y0 = digits_[0].y - PAD;
    uint8_t *px = pool.pixels[COLON_SLOT];
    std::memset(px, 0, (size_t) w * h);
    const float r = dot / 2.0f, centre_x = cx - x0;
    const float body = body_opa_ / 255.0f, rim_f = rim_opa_ / 255.0f;
    const float edge = 1.0f - (1.0f - body) * (1.0f - rim_f);
    const float soft = std::max(1.0f, rim_ * 1.8f);
    const float tall_px = glyph_h_ * unit_;
    const float centre_y[2] = {PAD + 0.34f * tall_px, PAD + 0.66f * tall_px};
    for (int y = 0; y < h; ++y) {
      const float py = y + 0.5f;
      const float lit = std::min(1.0f, std::max(0.0f, (py - PAD) / tall_px));
      const float body_y = std::min(1.0f, body * (1.25f - 0.45f * lit));
      for (int x = 0; x < w; ++x) {
        const float pxf = x + 0.5f;
        float best = 1e9f;
        for (int i = 0; i < 2; ++i) best = std::min(best, std::hypot(pxf - centre_x, py - centre_y[i]));
        const float cover = std::min(1.0f, std::max(0.0f, r + 0.5f - best));
        if (cover <= 0) continue;
        const float depth = std::max(0.0f, r - best);
        float glow = std::max(0.0f, 1.0f - depth / soft);
        glow *= glow;
        float alpha = (body_y + (edge - body_y) * glow) * cover;
        if (use_mask_) alpha *= 1.0f - mask_at(x0 + x + 0.5f, y0 + y + 0.5f);
        px[(size_t) y * w + x] = (uint8_t) std::lround(std::min(1.0f, alpha) * 255.0f);
      }
    }
    lv_image_dsc_t &dsc = pool.picture[COLON_SLOT];
    dsc = {};
    dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    dsc.header.cf = LV_COLOR_FORMAT_A8;
    dsc.header.w = w;
    dsc.header.h = h;
    dsc.header.stride = w;
    dsc.data = px;
    dsc.data_size = (size_t) w * h;
    lv_obj_t *img = lv_image_create(parent);
    lv_obj_remove_flag(img, LV_OBJ_FLAG_CLICKABLE);
    lv_image_set_src(img, &dsc);
    lv_obj_set_style_image_recolor(img, ui::color(theme::INK), 0);
    lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);
    lv_obj_set_pos(img, x0, y0);
    colon_ = img;
  }

  // Draw a digit into its picture. Every pixel gets its distance to the nearest centre line of the digit's strokes (a
  // bar is a segment, an arc a part of a circle). Within half a stroke of one the pixel is glass: a clear body that is
  // a little brighter at the top, and a soft bright edge all round the digit's outline. The strokes of a digit merge,
  // so there is no border across a join and no brighter spot where two strokes overlap.
  void raster_digit(int slot, char ch) {
    Digit &d = digits_[slot];
    if (d.shown == ch) return;
    d.shown = ch;
    RasterPool &pool = raster_pool();
    const lv_image_dsc_t &dsc = pool.picture[slot];
    const int w = dsc.header.w, h = dsc.header.h;
    uint8_t *px = pool.pixels[slot];
    std::memset(px, 0, (size_t) w * h);
    if (ch >= '0' && ch <= '9') {
      switch (style_) {
        case STYLE_3: draw_style3(slot, px, w, h, ch); break;
        default: draw_glass(slot, px, w, h, ch); break;
      }
    }
    lv_image_set_src(images_[slot], &dsc);  // the same picture again: LVGL draws what is in it now
    lv_obj_invalidate(images_[slot]);
  }

  // ---- The digit styles ----
  //
  // Each style fills the A8 picture `px` (w x h, one byte a pixel, already cleared) of the digit `ch` ('0' to '9') in slot
  // `slot`. The digit's box starts PAD pixels inside the picture and is unit_ wide and glyph_h_ * unit_ tall. To let the
  // overlay cut the digit out, multiply each pixel's alpha with (1 - cut_at(slot, x, y)) as draw_glass does.

  // The silhouette of the overlay at a pixel of a digit picture: 0 (nothing cut out) to 1 (cut out). 0 without a mask.
  float cut_at(int slot, int x, int y) const {
    if (!use_mask_) return 0.0f;
    return mask_at(digits_[slot].x - PAD + x + 0.5f, digits_[slot].y - PAD + y + 0.5f);
  }

  // Style 3: PLATZHALTER, bis der Code kommt. Zeichnet vorerst wie der Standard. (Form und Abstände: STYLES[2].)
  void draw_style3(int slot, uint8_t *px, int w, int h, char ch) { draw_glass(slot, px, w, h, ch); }

  // The frosted glass digits of styles 1 and 2: the shape (glyphs_, stroke_, glyph_h_) comes from the style.
  void draw_glass(int slot, uint8_t *px, int w, int h, char ch) {
    {
      struct Prim {
        bool arc, full;
        float a, b, c, d;       // a bar: both ends; an arc: its centre, then (c) the start and (d) the sweep in degrees
        float ex0, ey0, ex1, ey1;  // an arc's two end points
      };
      constexpr int MAX_PRIMS = 8;
      Prim prims[MAX_PRIMS];
      int n = 0;
      const Glyph &g = glyphs_[ch - '0'];
      const float r = radius_ * unit_;
      for (int i = 0; i < g.count && n < MAX_PRIMS; ++i) {
        const Seg &s = g.segs[i];
        Prim &q = prims[n++];
        q.arc = s.kind == ARC;
        q.full = false;
        if (!q.arc) {
          q.a = PAD + s.a * unit_;
          q.b = PAD + s.b * unit_;
          q.c = PAD + s.c * unit_;
          q.d = PAD + s.d * unit_;
        } else {
          q.a = PAD + s.a * unit_;
          q.b = PAD + s.b * unit_;
          q.full = s.d - s.c >= 359;
          q.c = std::fmod(std::fmod(s.c, 360.0f) + 360.0f, 360.0f);
          q.d = q.full ? 360.0f : std::fmod(std::fmod(s.d - s.c, 360.0f) + 360.0f, 360.0f);
          const float a0 = q.c * 3.14159265f / 180.0f, a1 = (q.c + q.d) * 3.14159265f / 180.0f;
          q.ex0 = q.a + r * std::cos(a0);
          q.ey0 = q.b + r * std::sin(a0);
          q.ex1 = q.a + r * std::cos(a1);
          q.ey1 = q.b + r * std::sin(a1);
        }
      }
      const float half = stroke_ * unit_ / 2;
      const float limit = half + 1.0f;
      const float body = body_opa_ / 255.0f, rim_f = rim_opa_ / 255.0f;
      const float edge = 1.0f - (1.0f - body) * (1.0f - rim_f);  // body and rim on one another
      const float soft = std::max(1.0f, rim_ * 1.8f);             // how far the bright edge reaches inwards
      const float tall_px = glyph_h_ * unit_;
      for (int y = 0; y < h; ++y) {
        const float py = y + 0.5f;
        // The body is a little brighter at the top than at the bottom, as light falls on glass.
        const float lit = std::min(1.0f, std::max(0.0f, (py - PAD) / tall_px));
        const float body_y = std::min(1.0f, body * (1.25f - 0.45f * lit));
        for (int x = 0; x < w; ++x) {
          const float pxf = x + 0.5f;
          float best = 1e9f;
          for (int i = 0; i < n; ++i) {
            const Prim &q = prims[i];
            float dist;
            if (!q.arc) {
              const float dx = q.c - q.a, dy = q.d - q.b;
              const float len2 = dx * dx + dy * dy;
              float t = len2 > 0 ? ((pxf - q.a) * dx + (py - q.b) * dy) / len2 : 0;
              t = std::min(1.0f, std::max(0.0f, t));
              dist = std::hypot(pxf - (q.a + t * dx), py - (q.b + t * dy));
            } else {
              const float ddx = pxf - q.a, ddy = py - q.b;
              const float ring = std::fabs(std::sqrt(ddx * ddx + ddy * ddy) - r);
              if (ring >= best || ring > limit) continue;
              if (q.full) {
                dist = ring;
              } else {
                float ang = std::atan2(ddy, ddx) * 57.2957795f;
                if (ang < 0) ang += 360.0f;
                float rel = ang - q.c;
                if (rel < 0) rel += 360.0f;
                dist = rel <= q.d ? ring
                                  : std::min(std::hypot(pxf - q.ex0, py - q.ey0), std::hypot(pxf - q.ex1, py - q.ey1));
              }
            }
            if (dist < best) best = dist;
          }
          if (best > limit) continue;
          const float cover = std::min(1.0f, std::max(0.0f, half + 0.5f - best));
          if (cover <= 0) continue;
          const float depth = std::max(0.0f, half - best);
          float glow = std::max(0.0f, 1.0f - depth / soft);
          glow *= glow;
          float alpha = (body_y + (edge - body_y) * glow) * cover;
          // Where the overlay's picture is opaque the digit is cut out, as if the overlay stood in front of it.
          if (use_mask_) alpha *= 1.0f - mask_at(digits_[slot].x - PAD + x + 0.5f, digits_[slot].y - PAD + y + 0.5f);
          px[(size_t) y * w + x] = (uint8_t) std::lround(std::min(1.0f, alpha) * 255.0f);
        }
      }
    }
  }

  enum Style : uint8_t { STYLE_GLASS, STYLE_2, STYLE_3 };
  Style style_ = STYLE_GLASS;
  int rim_ = 1;
  float unit_ = 50;
  float stroke_ = glass::T, glyph_h_ = glass::H, radius_ = glass::R;  // set from the style in show()
  const Glyph *glyphs_ = glass::GLYPHS;
  bool raster_ = false;
  lv_opa_t body_opa_ = LV_OPA_30, rim_opa_ = LV_OPA_70;
  uint32_t minute_ = 0;
  Digit digits_[4];
  // How much of the overlay's silhouette covers a point of the glass (in the glass's pixels), 0 to 1: the mask made at
  // build time, bilinear. 0 when there is no mask.
  float mask_at(float gx, float gy) const {
    const MaskData &m = mask_data();
    if (!m.data || m.w <= 0 || m.h <= 0 || glass_w_ <= 0 || glass_h_ <= 0) return 0.0f;
    const float u = gx / glass_w_ * m.w - 0.5f, v = gy / glass_h_ * m.h - 0.5f;
    const int x0 = (int) std::floor(u), y0 = (int) std::floor(v);
    const float fx = u - x0, fy = v - y0;
    auto at = [&m](int x, int y) {
      x = std::max(0, std::min(m.w - 1, x));
      y = std::max(0, std::min(m.h - 1, y));
      return m.data[y * m.w + x] / 255.0f;
    };
    return (at(x0, y0) * (1 - fx) + at(x0 + 1, y0) * fx) * (1 - fy) +
           (at(x0, y0 + 1) * (1 - fx) + at(x0 + 1, y0 + 1) * fx) * fy;
  }

  bool use_mask_ = false;
  int glass_w_ = 0, glass_h_ = 0;
  std::string overlay_url_;
  lv_obj_t *overlay_{}, *place_{}, *shade_{}, *date_{}, *colon_{}, *images_[SLOTS]{};
};

void GlassClock::set_mask(const uint8_t *data, int width, int height) {
  MaskData &m = mask_data();
  m.data = data;
  m.w = width;
  m.h = height;
}

void GlassClock::setup() {
  // The id is the saver's id in the manifest, and its step must be the manifest's (own).
  add_saver("glass", tessera::SaverStep::OWN, []() { return new GlassSaver(); });
}

}  // namespace esphome::glass_clock