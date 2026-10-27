#include "glass_clock.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace esphome::glass_clock {

using tessera::Font;
namespace ui = tessera::ui;

// A step of the screensaver of its own (plugin API 0.17): the time in tall, thin digits of frosted glass over the
// picture of an image entity, as the iPhone's lock screen draws it.
//
// A plugin has no font of its own, so the digits are drawn from rounded strokes: bars (a block with a bright border and
// a clear body) and arcs (the body, and a thin bright arc along each edge). Everything is made once in show(); a new
// minute only moves and shows the strokes of the new digits (never makes or deletes one). The screen moves the whole
// saver a millimetre every minute, so nothing stands still on the same pixels.
namespace {

// The shape of a digit, in units of its width: 1 wide, 1.83 tall, a stroke 0.17 thick. A bowl is a circle whose centre
// line has the radius RADIUS; the two bowls of an 8 touch in the middle.
constexpr float GLYPH_H = 1.83f;
constexpr float STROKE = 0.17f;
constexpr float RADIUS = (1.0f - STROKE) / 2;
constexpr float LEFT = STROKE / 2, RIGHT = 1.0f - STROKE / 2, TOP = STROKE / 2, BOTTOM = GLYPH_H - STROKE / 2;
constexpr float MID = 0.5f, UPPER = STROKE / 2 + RADIUS, LOWER = GLYPH_H - STROKE / 2 - RADIUS;

// A bar runs from (a, b) to (c, d) along its centre line. An arc is the part of a circle around (a, b) from the angle c
// to the angle d, clockwise, in degrees as LVGL counts them (0 east, 90 south).
//
// `open` says which ends of a stroke are free (not hidden inside another stroke): a free end is closed by a round cap
// with its own bright border. An arc has OPEN_START (the end at the angle c) and OPEN_END (the end at the angle d); a
// bar is a pill with a border all round when `open` is not 0, and has flat ends with a border on its two long sides
// when it is 0 (its ends lie in other strokes).
enum Kind : uint8_t { BAR, ARC };
enum Open : uint8_t { CLOSED = 0, OPEN_START = 1, OPEN_END = 2, OPEN_BOTH = 3 };
struct Seg {
  Kind kind;
  float a, b, c, d;
  uint8_t open;
};

constexpr Seg G0[] = {{ARC, MID, UPPER, 180, 360, CLOSED},
                      {ARC, MID, LOWER, 0, 180, CLOSED},
                      {BAR, LEFT, UPPER, LEFT, LOWER, CLOSED},
                      {BAR, RIGHT, UPPER, RIGHT, LOWER, CLOSED}};
constexpr Seg G1[] = {{BAR, 0.68f, TOP, 0.68f, BOTTOM, OPEN_BOTH},
                      {BAR, 0.30f, TOP, 0.68f, TOP, OPEN_BOTH}};
constexpr Seg G2[] = {{ARC, MID, UPPER-0.005f, 180, 450, OPEN_START},
                      {ARC, MID, LOWER, 90, 270, CLOSED},
                      {BAR, 0.50f, BOTTOM, RIGHT, BOTTOM, OPEN_BOTH}};
constexpr Seg G3[] = {{ARC, MID, UPPER, 200, 450, OPEN_BOTH},
                      {ARC, MID, LOWER, 270, 520, OPEN_BOTH}};
constexpr Seg G4[] = {{BAR, 0.75f, TOP, 0.75f, BOTTOM, OPEN_BOTH},
                      {BAR, LEFT, TOP, LEFT, 0.95f, OPEN_BOTH},
                      {BAR, LEFT, 0.95f, RIGHT, 0.95f, OPEN_BOTH}};
constexpr Seg G5[] = {{BAR, 0.15f, TOP, 0.15f, 1.1f, OPEN_BOTH},
                      {BAR, 0.15f, TOP, RIGHT, TOP, OPEN_BOTH},
                      {ARC, MID, LOWER, 215, 510, OPEN_BOTH}};
constexpr Seg G6[] = {{ARC, MID, LOWER, 0, 360, CLOSED},
                      {BAR, LEFT, UPPER, LEFT, LOWER, CLOSED},
                      {ARC, MID, UPPER, 180, 330, OPEN_END}};
constexpr Seg G7[] = {{BAR, LEFT, TOP, RIGHT, TOP, OPEN_BOTH},
                      {BAR, 0.91f, TOP, 0.91f, BOTTOM, OPEN_BOTH}};
constexpr Seg G8[] = {{ARC, MID, UPPER, 0, 360, CLOSED},
                      {ARC, MID, LOWER, 0, 360, CLOSED}};
constexpr Seg G9[] = {{ARC, MID, UPPER, 0, 360, CLOSED},
                      {BAR, RIGHT, UPPER, RIGHT, LOWER, CLOSED},
                      {ARC, MID, LOWER, 0, 150, OPEN_END}};

struct Glyph {
  const Seg *segs;
  uint8_t count;
};
constexpr Glyph GLYPHS[10] = {{G0, 4}, {G1, 2}, {G2, 3}, {G3, 2}, {G4, 3}, {G5, 3}, {G6, 3}, {G7, 2}, {G8, 2}, {G9, 3}};

// What one digit slot holds: room for the most any digit needs (two arcs of five layers, three bars).
constexpr int ARC_SLOTS = 2, BAR_SLOTS = 3, ARC_LAYERS = 5;
struct Digit {
  lv_obj_t *arcs[ARC_SLOTS][ARC_LAYERS]{};  // the body, the rim on each edge, and two layers for the caps of free ends
  lv_obj_t *bars[BAR_SLOTS]{};
  char shown = 0;    // '0' to '9', ' ' for none, 0 before the first time
  int x = 0, y = 0;  // top left, in the glass's pixels
};

// The shape of the smooth mode: much taller and thinner digits with oval bowls. A bowl is two arcs and two bars, so it can
// be as long as it needs to be; the two bowls of the 8 touch in the middle. Same units as above (1 wide). EXT is how far
// the straight sides of a bowl run, UP2 and LOW2 the centres of the arcs that close the upper and the lower bowl.
namespace tall {
constexpr float H = 3.0f, T = 0.15f, R = (1.0f - T) / 2;
constexpr float LEFT = T / 2, RIGHT = 1.0f - T / 2, TOP = T / 2, BOTTOM = H - T / 2, MID = 0.5f;
constexpr float UPPER = T / 2 + R, LOWER = H - T / 2 - R;
constexpr float EXT = H / 2 + T / 2 - 1.0f;
constexpr float UP2 = UPPER + EXT, LOW2 = LOWER - EXT;

constexpr Seg G0[] = {{ARC, MID, UPPER, 180, 360, CLOSED},
                      {ARC, MID, LOWER, 0, 180, CLOSED},
                      {BAR, LEFT, UPPER, LEFT, LOWER, CLOSED},
                      {BAR, RIGHT, UPPER, RIGHT, LOWER, CLOSED}};
constexpr Seg G1[] = {{BAR, 0.72f, TOP, 0.72f, BOTTOM, OPEN_BOTH},
                      {BAR, 0.16f, 0.42f, 0.72f, TOP, OPEN_BOTH}};
constexpr Seg G2[] = {{ARC, MID, UPPER, 180, 360, OPEN_START},
                      {BAR, RIGHT, UPPER, RIGHT, UP2, CLOSED},
                      {ARC, MID, UP2, 0, 25, CLOSED},
                      {BAR, 0.894f, UP2+0.184f, LEFT, BOTTOM, CLOSED},
                      {BAR, LEFT, BOTTOM, RIGHT, BOTTOM, OPEN_BOTH}};
constexpr Seg G3[] = {{ARC, MID, UPPER, 200, 360, OPEN_START},
                      {BAR, RIGHT, UPPER, RIGHT, UP2, CLOSED},
                      {ARC, MID, UP2, 0, 90, CLOSED},
                      {BAR, 0.30f, H/2, MID, H/2, OPEN_BOTH},
                      {ARC, MID, LOW2, 270, 360, CLOSED},
                      {BAR, RIGHT, LOW2, RIGHT, LOWER, CLOSED},
                      {ARC, MID, LOWER, 0, 160, OPEN_END}};
constexpr Seg G4[] = {{BAR, 0.74f, TOP, 0.74f, BOTTOM, OPEN_BOTH},
                      {BAR, 0.74f, TOP, LEFT, 0.70f*H, OPEN_BOTH},
                      {BAR, LEFT, 0.70f*H, RIGHT, 0.70f*H, OPEN_BOTH}};
constexpr Seg G5[] = {{BAR, 0.144f, TOP, 0.144f, 1.46f, OPEN_BOTH},
                      {BAR, 0.144f, TOP, RIGHT, TOP, OPEN_BOTH},
                      {ARC, MID, 1.7f, 215, 360, CLOSED},
                      {BAR, RIGHT, 1.7f, RIGHT, LOWER, CLOSED},
                      {ARC, MID, LOWER, 0, 155, OPEN_END}};
constexpr Seg G6[] = {{ARC, MID, 1.7f, 180, 360, CLOSED},
                      {ARC, MID, LOWER, 0, 180, CLOSED},
                      {BAR, LEFT, LOW2, LEFT, LOWER, CLOSED},
                      {BAR, RIGHT, 1.7f, RIGHT, LOWER, CLOSED},
                      {BAR, LEFT, UPPER, LEFT, LOW2, CLOSED},
                      {ARC, MID, UPPER, 180, 335, OPEN_END}};
constexpr Seg G7[] = {{BAR, LEFT, TOP, RIGHT, TOP, OPEN_BOTH},
                      {BAR, RIGHT, TOP, 0.28f, BOTTOM, OPEN_BOTH}};
constexpr Seg G8[] = {{ARC, MID, UPPER, 180, 360, CLOSED},
                      {ARC, MID, UP2, 0, 180, CLOSED},
                      {BAR, LEFT, UPPER, LEFT, UP2, CLOSED},
                      {BAR, RIGHT, UPPER, RIGHT, UP2, CLOSED},
                      {ARC, MID, LOW2, 180, 360, CLOSED},
                      {ARC, MID, LOWER, 0, 180, CLOSED},
                      {BAR, LEFT, LOW2, LEFT, LOWER, CLOSED},
                      {BAR, RIGHT, LOW2, RIGHT, LOWER, CLOSED}};
constexpr Seg G9[] = {{ARC, MID, UPPER, 180, 360, CLOSED},
                      {ARC, MID, 1.3f, 0, 180, CLOSED},
                      {BAR, LEFT, 0.5f, LEFT, 1.3f, CLOSED},
                      {BAR, RIGHT, UPPER, RIGHT, UP2, CLOSED},
                      {BAR, RIGHT, UP2, RIGHT, LOWER, CLOSED},
                      {ARC, MID, LOWER, 0, 155, OPEN_END}};
constexpr Glyph GLYPHS[10] = {{G0, 4}, {G1, 2}, {G2, 5}, {G3, 7}, {G4, 3}, {G5, 5}, {G6, 6}, {G7, 2}, {G8, 8}, {G9, 6}};
}  // namespace tall

// The pictures of the smooth mode (A8, one byte a pixel: how much of the ink shows), one per digit slot. They are kept in
// PSRAM for as long as the firmware runs and reused by the next saver, so nothing that LVGL still holds is ever freed.
constexpr int SLOTS = 4;
constexpr int PAD = 2;  // pixels around a digit's box, for the soft edge
struct RasterPool {
  uint8_t *pixels[SLOTS] = {};
  size_t capacity[SLOTS] = {};
  lv_image_dsc_t picture[SLOTS] = {};
};
RasterPool &raster_pool() {
  static RasterPool pool;
  return pool;
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
    seconds_ = c.options["seconds"] | true;
    // How many layers an arc is drawn from: 5 (body, both edges, the caps of free ends), 3 (no caps) or 1 (body only).
    // Every layer is an LVGL object of its own, so fewer layers leave the screen more memory.
    // `mode`: "raster" draws each digit as one soft picture (a few objects, glass with a continuous bright edge), "strokes"
    // builds it from arcs and bars (the first way, many objects).
    const bool want_raster = std::strcmp(c.options["mode"] | "raster", "strokes") != 0;
    const char *detail = c.options["detail"] | "light";
    layers_ = std::strcmp(detail, "full") == 0 ? 5 : (std::strcmp(detail, "minimal") == 0 ? 1 : 3);
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
    shade_ = ui::block(c.parent, theme::CAMERA_PAGE);
    lv_obj_set_style_radius(shade_, 0, 0);
    lv_obj_set_style_bg_opa(shade_, (lv_opa_t) (dim * 255 / 100), 0);
    lv_obj_set_size(shade_, c.width, c.height);
    lv_obj_set_pos(shade_, 0, 0);

    // The layout: four digits in a row, or two rows of two. The smooth mode has its own, much taller shape (the table
    // in `tall`); the strokes keep the first. `arrange` fixes the metrics, the size and the places of the digits; with
    // `auto` the arrangement that makes the digits larger wins.
    constexpr float IN = 0.15f, COLON = 0.45f, ROWS = 0.30f;
    const int date_h = date ? ui::line_height(Font::TITLE) : 0;
    const int date_gap = date ? ui::px(10) : 0;
    bool stacked = false;
    int top = 0;
    auto arrange = [&](bool smooth) {
      stroke_ = smooth ? tall::T : STROKE;
      glyph_h_ = smooth ? tall::H : GLYPH_H;
      radius_ = (1.0f - stroke_) / 2;
      glyphs_ = smooth ? tall::GLYPHS : GLYPHS;
      const float room = (float) (c.height - date_h - date_gap);
      const float side = std::min(c.width * 0.92f / (4 + 2 * IN + COLON), room * 0.62f / glyph_h_);
      const float stack = std::min(c.width * 0.84f / (2 + IN), room * 0.86f / (2 * glyph_h_ + ROWS));
      stacked = std::strcmp(layout, "stacked") == 0 || (std::strcmp(layout, "side") != 0 && stack > side);
      unit_ = stacked ? stack : side;
      thick_ = std::max(3, (int) std::lround(stroke_ * unit_));
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
        const int x0 = (c.width - (int) std::lround((4 + 2 * IN + COLON) * unit_)) / 2;
        const int in = (int) std::lround((1 + IN) * unit_), colon = (int) std::lround((1 + COLON) * unit_);
        digits_[0].x = x0;
        digits_[1].x = x0 + in;
        digits_[2].x = x0 + in + colon;
        digits_[3].x = x0 + 2 * in + colon;
        for (Digit &d : digits_) d.y = top;
      }
    };
    arrange(want_raster);
    raster_ = want_raster && reserve_rasters();
    if (want_raster && !raster_) arrange(false);  // no room for the pictures: the strokes, with their own shape

    if (date) {
      date_ = ui::label(c.parent, Font::TITLE, theme::INK_SOFT);
      lv_obj_set_width(date_, c.width);
      lv_obj_set_style_text_align(date_, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_set_pos(date_, 0, top - date_gap - date_h);
    }
    if (!stacked) {
      // The colon: two glass dots between the pairs, at a third and two thirds of the digits' height.
      const int dot = std::max(4, (int) std::lround(0.16f * unit_));
      const int cx = digits_[1].x + (int) std::lround(unit_) + (int) std::lround(COLON * unit_ / 2);
      for (int i = 0; i < 2; ++i) {
        dots_[i] = glass_block(c.parent);
        lv_obj_set_size(dots_[i], dot, dot);
        lv_obj_set_pos(dots_[i], cx - dot / 2,
                       top + (int) std::lround((i == 0 ? 0.34f : 0.66f) * glyph_h_ * unit_) - dot / 2);
      }
    }
    if (raster_) create_images(c.parent);
    else
      for (Digit &d : digits_) make_digit(c.parent, d);
  }

  // The step's data: its entity's id, `entity`, which the picture's address is made of. A picture comes from the app,
  // by that address, exactly as it does for Tessera's own tiles.
  void on_state(JsonObjectConst data) override {
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
    // The colon blinks with the seconds: only its two small boxes are drawn again.
    if (dots_[0] && seconds_) {
      const bool hidden = lv_obj_has_flag(dots_[0], LV_OBJ_FLAG_HIDDEN);
      if ((epoch % 2 == 0) == hidden) {
        for (lv_obj_t *dot : dots_) {
          if (epoch % 2 == 0) lv_obj_remove_flag(dot, LV_OBJ_FLAG_HIDDEN);
          else lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
        }
      }
    }
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
  // A piece of glass: a clear body with a bright border, in the ink's colour, with a pill's corners.
  lv_obj_t *glass_block(lv_obj_t *parent) {
    lv_obj_t *b = ui::block(parent, theme::INK);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(b, body_opa_, 0);
    lv_obj_set_style_border_width(b, rim_, 0);
    lv_obj_set_style_border_color(b, ui::color(theme::INK), 0);
    lv_obj_set_style_border_opa(b, rim_opa_, 0);
    return b;
  }

  // One layer of an arc: an LVGL arc with nothing but its background arc visible (no knob, no indicator, no touch).
  lv_obj_t *arc_layer(lv_obj_t *parent, lv_opa_t opa) {
    lv_obj_t *a = lv_arc_create(parent);
    lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(a, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(a, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(a, 0, 0);
    lv_obj_set_style_outline_width(a, 0, 0);
    lv_obj_set_style_shadow_width(a, 0, 0);
    lv_obj_set_style_pad_all(a, 0, 0);
    lv_obj_set_style_arc_opa(a, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(a, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_arc_color(a, ui::color(theme::INK), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(a, opa, LV_PART_MAIN);
    lv_arc_set_rotation(a, 0);
    lv_obj_add_flag(a, LV_OBJ_FLAG_HIDDEN);
    return a;
  }

  // Every part of a digit slot, made once and hidden until a digit needs it.
  void make_digit(lv_obj_t *parent, Digit &d) {
    for (auto &arc : d.arcs) {
      arc[0] = arc_layer(parent, body_opa_);
      for (int i = 1; i < layers_; ++i) arc[i] = arc_layer(parent, rim_opa_);
    }
    for (lv_obj_t *&bar : d.bars) {
      bar = glass_block(parent);
      lv_obj_add_flag(bar, LV_OBJ_FLAG_HIDDEN);
    }
  }

  // ---- The smooth mode ----

  void put(int slot, char ch) {
    if (raster_) raster_digit(slot, ch);
    else paint_digit(digits_[slot], ch);
  }

  // Room for the pictures of the digit slots, in PSRAM. False when it is not there.
  bool reserve_rasters() {
    RasterPool &pool = raster_pool();
    const size_t bytes = (size_t) picture_w() * picture_h();
    for (int i = 0; i < SLOTS; ++i) {
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
          const float alpha = (body_y + (edge - body_y) * glow) * cover;
          px[(size_t) y * w + x] = (uint8_t) std::lround(std::min(1.0f, alpha) * 255.0f);
        }
      }
    }
    lv_image_set_src(images_[slot], &dsc);  // the same picture again: LVGL draws what is in it now
    lv_obj_invalidate(images_[slot]);
  }

  // Show a digit in its slot: place the strokes it needs, hide the rest. Nothing happens when it already shows it.
  void paint_digit(Digit &d, char ch) {
    if (d.shown == ch) return;
    d.shown = ch;
    int arcs = 0, bars = 0;
    if (ch >= '0' && ch <= '9') {
      const Glyph &g = GLYPHS[ch - '0'];
      for (int i = 0; i < g.count; ++i) {
        const Seg &s = g.segs[i];
        if (s.kind == ARC && arcs < ARC_SLOTS) place_arc(d.arcs[arcs++], d, s);
        else if (s.kind == BAR && bars < BAR_SLOTS) place_bar(d.bars[bars++], d, s);
      }
    }
    for (int i = arcs; i < ARC_SLOTS; ++i)
      for (lv_obj_t *layer : d.arcs[i])
        if (layer) lv_obj_add_flag(layer, LV_OBJ_FLAG_HIDDEN);
    for (int i = bars; i < BAR_SLOTS; ++i) lv_obj_add_flag(d.bars[i], LV_OBJ_FLAG_HIDDEN);
  }

  // An arc: the body as wide as the stroke, a thin bright arc along each edge of it, and the caps of its free ends.
  // LVGL draws an arc inward from the edge of its object, so a layer's object is as large as the outer edge of what it
  // draws. A free end is closed by a half circle of the stroke's width: with both ends free the body's own round caps
  // are the fill and the two caps only draw the border; with one end free the body has flat ends, and the cap is a
  // filled half circle (an arc as wide as its radius) and its border.
  void place_arc(lv_obj_t *const layers[ARC_LAYERS], const Digit &d, const Seg &s) {
    const int cx = d.x + (int) std::lround(s.a * unit_), cy = d.y + (int) std::lround(s.b * unit_);
    const int r = (int) std::lround(RADIUS * unit_);
    const int outer = r + thick_ / 2, inner = r - thick_ / 2;
    const bool full = s.d - s.c >= 359;
    // LVGL reads the angles from 0 to 359; a whole circle is 0 to 359 with flat ends, and the 1 degree is not seen.
    const int start = full ? 0 : (int) s.c % 360, end = full ? 359 : (int) s.d % 360;
    const bool open_start = !full && (s.open & OPEN_START), open_end = !full && (s.open & OPEN_END);
    struct Layer {
      int x, y, radius, width, from, to;  // the centre, the radius of the object, the width, the angles
      lv_opa_t opa;
      bool rounded, shown;
    };
    Layer look[ARC_LAYERS] = {};
    look[0] = {cx, cy, outer, thick_, start, end, body_opa_, layers_ < 5 ? (open_start || open_end) : (open_start && open_end), true};
    look[1] = {cx, cy, outer, rim_, start, end, rim_opa_, false, true};
    look[2] = {cx, cy, inner + rim_, rim_, start, end, rim_opa_, false, true};
    // The caps: a half circle around the end's centre line point, on the side the stroke runs out to.
    const int cap = thick_ / 2;
    auto cap_at = [&](int angle, bool at_start, int slot, bool fill) {
      const float rad = angle * 3.14159265f / 180.0f;
      const int px = cx + (int) std::lround(r * std::cos(rad)), py = cy + (int) std::lround(r * std::sin(rad));
      const int from = at_start ? (angle + 180) % 360 : angle % 360, to = at_start ? angle % 360 : (angle + 180) % 360;
      look[slot] = {px, py, cap, fill ? cap : rim_, from, to, fill ? body_opa_ : rim_opa_, false, true};
    };
    if (layers_ < 5) {
      // no caps: the body's own round ends close a free end, without a border of their own
    } else if (open_start && open_end) {
      cap_at(start, true, 3, false);
      cap_at(end, false, 4, false);
    } else if (open_start) {
      cap_at(start, true, 3, true);
      cap_at(start, true, 4, false);
    } else if (open_end) {
      cap_at(end, false, 3, true);
      cap_at(end, false, 4, false);
    }
    for (int i = 0; i < layers_; ++i) {
      lv_obj_t *a = layers[i];
      const Layer &l = look[i];
      if (!l.shown) {
        lv_obj_add_flag(a, LV_OBJ_FLAG_HIDDEN);
        continue;
      }
      lv_obj_set_size(a, 2 * l.radius, 2 * l.radius);
      lv_obj_set_pos(a, l.x - l.radius, l.y - l.radius);
      lv_arc_set_bg_angles(a, l.from, l.to);
      lv_obj_set_style_arc_width(a, l.width, LV_PART_MAIN);
      lv_obj_set_style_arc_opa(a, l.opa, LV_PART_MAIN);
      lv_obj_set_style_arc_rounded(a, l.rounded, LV_PART_MAIN);
      lv_obj_remove_flag(a, LV_OBJ_FLAG_HIDDEN);
    }
  }

  // A bar: a pill along its centre line, or with flat ends and a border on its two long sides where it joins others.
  void place_bar(lv_obj_t *bar, const Digit &d, const Seg &s) {
    const bool vertical = std::fabs(s.a - s.c) < 0.001f;
    const bool joined = s.open == CLOSED;
    const int cap = joined ? 0 : thick_ / 2;
    int x0 = d.x + (int) std::lround(std::min(s.a, s.c) * unit_);
    int x1 = d.x + (int) std::lround(std::max(s.a, s.c) * unit_);
    int y0 = d.y + (int) std::lround(std::min(s.b, s.d) * unit_);
    int y1 = d.y + (int) std::lround(std::max(s.b, s.d) * unit_);
    if (vertical) {
      x0 -= thick_ / 2;
      x1 = x0 + thick_;
      y0 -= cap;
      y1 += cap;
    } else {
      y0 -= thick_ / 2;
      y1 = y0 + thick_;
      x0 -= cap;
      x1 += cap;
    }
    lv_obj_set_pos(bar, x0, y0);
    lv_obj_set_size(bar, x1 - x0, y1 - y0);
    lv_obj_set_style_radius(bar, joined ? 0 : LV_RADIUS_CIRCLE, 0);
    const lv_border_side_t sides = !joined ? LV_BORDER_SIDE_FULL
                                   : vertical ? (lv_border_side_t) (LV_BORDER_SIDE_LEFT | LV_BORDER_SIDE_RIGHT)
                                              : (lv_border_side_t) (LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_BOTTOM);
    lv_obj_set_style_border_side(bar, sides, 0);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_HIDDEN);
  }

  int thick_ = 3, rim_ = 1, layers_ = 3;
  float unit_ = 50, stroke_ = STROKE, glyph_h_ = GLYPH_H, radius_ = RADIUS;
  const Glyph *glyphs_ = GLYPHS;
  bool seconds_ = true, raster_ = false;
  lv_opa_t body_opa_ = LV_OPA_30, rim_opa_ = LV_OPA_70;
  uint32_t minute_ = 0;
  Digit digits_[4];
  lv_obj_t *place_{}, *shade_{}, *date_{}, *dots_[2]{}, *images_[SLOTS]{};
};

void GlassClock::setup() {
  // The id is the saver's id in the manifest, and its step must be the manifest's (own).
  add_saver("glass", tessera::SaverStep::OWN, []() { return new GlassSaver(); });
}

}  // namespace esphome::glass_clock