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
// to the angle d, clockwise, in degrees as LVGL counts them (0 east, 90 south). `joined`: its ends lie in other
// strokes, so a bar has flat ends and no border across them, and an arc has flat ends.
enum Kind : uint8_t { BAR, ARC };
struct Seg {
  Kind kind;
  float a, b, c, d;
  bool joined;
};

constexpr Seg G0[] = {{ARC, MID, UPPER, 180, 360, true}, {ARC, MID, LOWER, 0, 180, true},
                      {BAR, LEFT, UPPER, LEFT, LOWER, true}, {BAR, RIGHT, UPPER, RIGHT, LOWER, true}};
constexpr Seg G1[] = {{BAR, 0.68f, TOP, 0.68f, BOTTOM, false}, {BAR, 0.30f, TOP, 0.68f, TOP, false}};
constexpr Seg G2[] = {{ARC, MID, UPPER, 180, 450, false}, {ARC, MID, LOWER, 90, 270, false},
                      {BAR, LEFT, BOTTOM, RIGHT, BOTTOM, false}};
constexpr Seg G3[] = {{ARC, MID, UPPER, 200, 450, false}, {ARC, MID, LOWER, 270, 520, false}};
constexpr Seg G4[] = {{BAR, 0.75f, TOP, 0.75f, BOTTOM, false}, {BAR, LEFT, TOP, LEFT, 1.15f, false},
                      {BAR, LEFT, 1.15f, RIGHT, 1.15f, false}};
constexpr Seg G5[] = {{BAR, LEFT, TOP, LEFT, 1.0f, false}, {BAR, LEFT, TOP, RIGHT, TOP, false},
                      {ARC, MID, LOWER, 215, 510, false}};
constexpr Seg G6[] = {{ARC, MID, LOWER, 0, 360, true}, {BAR, LEFT, UPPER, LEFT, LOWER, true},
                      {ARC, MID, UPPER, 180, 330, false}};
constexpr Seg G7[] = {{BAR, LEFT, TOP, RIGHT, TOP, false}, {BAR, 0.70f, TOP, 0.70f, BOTTOM, false}};
constexpr Seg G8[] = {{ARC, MID, UPPER, 0, 360, true}, {ARC, MID, LOWER, 0, 360, true}};
constexpr Seg G9[] = {{ARC, MID, UPPER, 0, 360, true}, {BAR, RIGHT, UPPER, RIGHT, LOWER, true},
                      {ARC, MID, LOWER, 0, 150, false}};

struct Glyph {
  const Seg *segs;
  uint8_t count;
};
constexpr Glyph GLYPHS[10] = {{G0, 4}, {G1, 2}, {G2, 3}, {G3, 2}, {G4, 3}, {G5, 3}, {G6, 3}, {G7, 2}, {G8, 2}, {G9, 3}};

// What one digit slot holds: room for the most any digit needs (two arcs of three layers, three bars).
constexpr int ARC_SLOTS = 2, BAR_SLOTS = 3, ARC_LAYERS = 3;
struct Digit {
  lv_obj_t *arcs[ARC_SLOTS][ARC_LAYERS]{};  // the body, the rim on the outer edge, the rim on the inner edge
  lv_obj_t *bars[BAR_SLOTS]{};
  char shown = 0;    // '0' to '9', ' ' for none, 0 before the first time
  int x = 0, y = 0;  // top left, in the glass's pixels
};

}  // namespace

class GlassSaver : public tessera::Saver {
 public:
  void show(const tessera::SaverContext &c) override {
    // An option the person never touched may not be in the word: read each with its default.
    const char *layout = c.options["layout"] | "auto";
    const int dim = std::max(0, std::min(80, (int) (c.options["dim"] | 35)));
    const bool date = c.options["date"] | true;
    seconds_ = c.options["seconds"] | true;
    // The glass gets a little fainter at night, when the level is low and a face may go darker still.
    body_opa_ = c.night ? LV_OPA_20 : LV_OPA_30;
    rim_opa_ = c.night ? LV_OPA_50 : LV_OPA_70;

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

    // The layout: four digits in a row on a wide glass, two rows of two on a square or tall one.
    const bool stacked = std::strcmp(layout, "stacked") == 0 ||
                         (std::strcmp(layout, "side") != 0 && c.width * 100 < c.height * 135);
    constexpr float IN = 0.22f, COLON = 0.56f, ROWS = 0.30f;
    const int date_h = date ? ui::line_height(Font::TITLE) : 0;
    const int date_gap = date ? ui::px(10) : 0;
    if (stacked) {
      unit_ = std::min(c.width * 0.84f / (2 + IN), (c.height - date_h - date_gap) * 0.86f / (2 * GLYPH_H + ROWS));
    } else {
      unit_ = std::min(c.width * 0.92f / (4 + 2 * IN + COLON), (c.height - date_h - date_gap) * 0.62f / GLYPH_H);
    }
    thick_ = std::max(3, (int) std::lround(STROKE * unit_));
    rim_ = std::max(1, (int) std::lround(0.022f * unit_));
    const int unit = (int) std::lround(unit_);
    const int digits_h = (int) std::lround((stacked ? 2 * GLYPH_H + ROWS : GLYPH_H) * unit_);
    const int top = (c.height - date_h - date_gap - digits_h) / 2 + date_h + date_gap;
    if (date) {
      date_ = ui::label(c.parent, Font::TITLE, theme::INK_SOFT);
      lv_obj_set_width(date_, c.width);
      lv_obj_set_style_text_align(date_, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_set_pos(date_, 0, top - date_gap - date_h);
    }
    if (stacked) {
      const int x0 = (c.width - (int) std::lround((2 + IN) * unit_)) / 2;
      const int step = (int) std::lround((1 + IN) * unit_);
      const int row = (int) std::lround((GLYPH_H + ROWS) * unit_);
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
      // The colon: two glass dots between the pairs, at a third and two thirds of the digits' height.
      const int dot = std::max(4, (int) std::lround(0.16f * unit_));
      const int cx = digits_[1].x + unit + (int) std::lround(COLON * unit_ / 2);
      for (int i = 0; i < 2; ++i) {
        dots_[i] = glass_block(c.parent);
        lv_obj_set_size(dots_[i], dot, dot);
        lv_obj_set_pos(dots_[i], cx - dot / 2, top + (int) std::lround((i == 0 ? 0.62f : 1.21f) * unit_) - dot / 2);
      }
    }
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
    paint_digit(digits_[0], hours.size() > 1 ? hours[hours.size() - 2] : ' ');
    paint_digit(digits_[1], hours.back());
    paint_digit(digits_[2], clock[colon + 1]);
    paint_digit(digits_[3], clock[colon + 2]);
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
      arc[1] = arc_layer(parent, rim_opa_);
      arc[2] = arc_layer(parent, rim_opa_);
    }
    for (lv_obj_t *&bar : d.bars) {
      bar = glass_block(parent);
      lv_obj_add_flag(bar, LV_OBJ_FLAG_HIDDEN);
    }
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
      for (lv_obj_t *layer : d.arcs[i]) lv_obj_add_flag(layer, LV_OBJ_FLAG_HIDDEN);
    for (int i = bars; i < BAR_SLOTS; ++i) lv_obj_add_flag(d.bars[i], LV_OBJ_FLAG_HIDDEN);
  }

  // An arc of three layers: the body as wide as the stroke, and a thin bright arc along each edge of it. LVGL draws an
  // arc inward from the edge of its object, so a layer's object is as large as the outer edge of what it draws.
  void place_arc(lv_obj_t *const layers[ARC_LAYERS], const Digit &d, const Seg &s) {
    const int cx = d.x + (int) std::lround(s.a * unit_), cy = d.y + (int) std::lround(s.b * unit_);
    const int r = (int) std::lround(RADIUS * unit_);
    const int outer = r + thick_ / 2, inner = r - thick_ / 2;
    const bool full = s.d - s.c >= 359;
    // LVGL reads the angles from 0 to 359; a whole circle is 0 to 359 with flat ends, and the 1 degree is not seen.
    const int start = full ? 0 : (int) s.c % 360, end = full ? 359 : (int) s.d % 360;
    const struct {
      int radius, width;
    } look[ARC_LAYERS] = {{outer, thick_}, {outer, rim_}, {inner + rim_, rim_}};
    for (int i = 0; i < ARC_LAYERS; ++i) {
      lv_obj_t *a = layers[i];
      lv_obj_set_size(a, 2 * look[i].radius, 2 * look[i].radius);
      lv_obj_set_pos(a, cx - look[i].radius, cy - look[i].radius);
      lv_arc_set_bg_angles(a, start, end);
      lv_obj_set_style_arc_width(a, look[i].width, LV_PART_MAIN);
      lv_obj_set_style_arc_rounded(a, !s.joined && !full, LV_PART_MAIN);
      lv_obj_remove_flag(a, LV_OBJ_FLAG_HIDDEN);
    }
  }

  // A bar: a pill along its centre line, or with flat ends and a border on its two long sides where it joins others.
  void place_bar(lv_obj_t *bar, const Digit &d, const Seg &s) {
    const bool vertical = std::fabs(s.a - s.c) < 0.001f;
    const int cap = s.joined ? 0 : thick_ / 2;
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
    lv_obj_set_style_radius(bar, s.joined ? 0 : LV_RADIUS_CIRCLE, 0);
    const lv_border_side_t sides = !s.joined ? LV_BORDER_SIDE_FULL
                                   : vertical ? (lv_border_side_t) (LV_BORDER_SIDE_LEFT | LV_BORDER_SIDE_RIGHT)
                                              : (lv_border_side_t) (LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_BOTTOM);
    lv_obj_set_style_border_side(bar, sides, 0);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_HIDDEN);
  }

  int thick_ = 3, rim_ = 1;
  float unit_ = 50;
  bool seconds_ = true;
  lv_opa_t body_opa_ = LV_OPA_30, rim_opa_ = LV_OPA_70;
  uint32_t minute_ = 0;
  Digit digits_[4];
  lv_obj_t *place_{}, *shade_{}, *date_{}, *dots_[2]{};
};

void GlassClock::setup() {
  // The id is the saver's id in the manifest, and its step must be the manifest's (own).
  add_saver("glass", tessera::SaverStep::OWN, []() { return new GlassSaver(); });
}

}  // namespace esphome::glass_clock
