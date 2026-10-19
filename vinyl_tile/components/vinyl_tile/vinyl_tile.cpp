#include "vinyl_tile.h"
#include <algorithm>
#include <cstdio>
#include <string>

namespace esphome::vinyl_tile {

using tessera::Font;
namespace ui = tessera::ui;

// Home Assistant's MediaPlayerEntityFeature: the bits of `supported_features` that decide the keys.
constexpr uint32_t PAUSE = 1, VOLUME_SET = 4, VOLUME_MUTE = 8, VOLUME_STEP = 1024, PLAY = 16384;
// Icons of Tessera's set.
constexpr uint32_t ICON_PLAY = 0xF040A, ICON_PAUSE = 0xF03E4, ICON_VOLUME_UP = 0xF075D, ICON_VOLUME_DOWN = 0xF075E,
                   ICON_MUTED = 0xF0581;
// The keys, left to right.
enum Key : uint8_t { MUTE, DOWN, PLAY_PAUSE, UP, KEYS, NONE = KEYS };

// The tile of a media player that fills the screen: the cover as a record sleeve with the record peeking out at its
// right, the title and the artist beside it (under it on square and standing glass), and four keys. It is the layout of
// the Vinyl deck screensaver, as a tile: it has an on_theme, a tap that comes from the whole card, and no standby.
//
// The cover is the player's own picture (entity_picture). A tap on a key calls tessera::wish for play, pause and the
// mute, so the key shows its other face at once and Home Assistant squares it; a volume step has nothing to show and
// goes as an action. A tap anywhere else does nothing.
class DeckTile : public tessera::Tile {
 public:
  explicit DeckTile(const tessera::Plugin *plugin) : plugin_(plugin) {}

  void create(const tessera::TileContext &c) override {
    keys_on_ = c.options["keys"] | true;
    std::snprintf(entity_, sizeof(entity_), "%s", c.entity);
    const int m = ui::mm(4), gap = ui::mm(3);
    const int title_h = ui::line_height(Font::HEADLINE), artist_h = ui::line_height(Font::BODY_LARGE);
    const bool wide = c.width * 10 >= c.height * 13;
    int side, x, y, text_x, text_y, text_w, lines;
    if (wide) {
      // The sleeve at the left, as high as the glass allows; the words and the keys in the column beside the record.
      side = std::min(c.height - 2 * m, c.width * 45 / 100);
      x = m;
      y = (c.height - side) / 2;
      text_x = x + side * 122 / 100 + ui::mm(4);
      text_w = c.width - text_x - m;
      lines = 2;
    } else {
      // The sleeve on top, one line of title, the artist and the keys under it, all in the middle.
      lines = 1;
      text_x = m;
      text_w = c.width - 2 * m;
    }
    // Four keys of a finger's size fit in the width of the words, however narrow it is.
    key_ = std::min(ui::mm(11), (text_w - 3 * gap) / 4);
    gap_ = gap;
    if (wide) {
      const int block = lines * title_h + artist_h + 2 * gap + key_;
      text_y = std::max(m, y + (side - block) / 2);
    } else {
      text_y = c.height - m - key_ - 2 * gap - artist_h - title_h;
      side = std::min(text_y - gap - m, (c.width - 2 * m) * 100 / 122);
      x = (c.width - side * 122 / 100) / 2;
      y = m + (text_y - gap - m - side) / 2;
    }
    record(c.parent, x + side * 30 / 100, y + side * 4 / 100, side * 92 / 100);
    // The sleeve, made after the record so it lies over it. A board without pictures shows the record alone.
    if (ui::pictures()) {
      sleeve_ = ui::picture(c.parent);
      lv_obj_set_size(sleeve_, side, side);
      lv_obj_set_pos(sleeve_, x, y);
    }
    const lv_text_align_t align = wide ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_CENTER;
    title_ = ui::text(c.parent, Font::HEADLINE, theme::INK);
    lv_obj_set_size(title_, text_w, lines * title_h);
    lv_obj_set_pos(title_, text_x, text_y);
    lv_obj_set_style_text_align(title_, align, 0);
    artist_ = ui::label(c.parent, Font::BODY_LARGE, theme::MUTED);
    lv_obj_set_width(artist_, text_w);
    lv_obj_set_pos(artist_, text_x, text_y + lines * title_h);
    lv_obj_set_style_text_align(artist_, align, 0);
    // The keys: round, an icon in each, laid out in on_state once the player says which it has.
    keys_x_ = text_x;
    keys_w_ = wide ? 0 : text_w;  // 0: from the left of the column; else in the middle of this width
    keys_y_ = text_y + lines * title_h + artist_h + 2 * gap;
    for (auto &k : keys_) {
      k = ui::block(c.parent, theme::RAISED);
      lv_obj_set_style_radius(k, LV_RADIUS_CIRCLE, 0);
      lv_obj_set_size(k, key_, key_);
      lv_obj_t *icon = ui::label(k, Font::ICON, theme::INK);
      lv_obj_set_width(icon, key_);
      lv_obj_set_style_text_align(icon, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_set_pos(icon, 0, (key_ - ui::line_height(Font::ICON)) / 2);
      lv_obj_add_flag(k, LV_OBJ_FLAG_HIDDEN);
    }
    ui::set_text(title_, plugin_->text("choose"));
  }

  // What the entity says: its state and the attributes the manifest names. Before a person has chosen a player there
  // is nothing, and the tile says so.
  void on_state(JsonObjectConst data) override {
    const std::string state = data["state"] | "";
    JsonObjectConst a = data["attributes"];
    const bool has_player = !state.empty();
    const bool idle = state == "idle" || state == "off" || state == "standby" || state == "unavailable" ||
                      state == "unknown";
    playing_ = state == "playing";
    muted_ = a["is_volume_muted"] | false;
    features_ = a["supported_features"] | 0u;
    const std::string title = a["media_title"] | "";
    if (!has_player) {
      ui::set_text(title_, plugin_->text("choose"));
      ui::set_text(artist_, "");
    } else if (idle || title.empty()) {
      ui::set_text(title_, plugin_->text("nothing"));
      ui::set_text(artist_, "");
    } else {
      ui::set_text(title_, title.c_str());
      ui::set_text(artist_, a["media_artist"] | "");
    }
    // The player's own cover; the picture goes while nothing plays.
    if (sleeve_) ui::set_picture(sleeve_, (has_player && !idle) ? (a["entity_picture"] | "") : "", tessera::Fit::COVER);
    paint_keys();
  }

  void on_theme() override {
    ui::set_color(title_, theme::INK);
    ui::set_color(artist_, theme::MUTED);
    paint_record();
    paint_keys();
  }

  void on_tap() override {
    if (!entity_[0]) return;
    const Key key = key_at(tessera::tap_point());
    if (key == NONE) return;
    if (key == PLAY_PAUSE) {
      tessera::wish(entity_, tessera::Wish::PLAYING, playing_ ? "0" : "1");
    } else if (key == MUTE) {
      tessera::wish(entity_, tessera::Wish::MUTED, muted_ ? "0" : "1");
    } else if (muted_ && (features_ & VOLUME_MUTE)) {
      // A muted player: the first tap on a volume key takes the mute off and changes nothing else.
      tessera::wish(entity_, tessera::Wish::MUTED, "0");
    } else {
      tessera::action(key == UP ? "media_player.volume_up" : "media_player.volume_down", entity_);
    }
  }

 private:
  // The record: a grey disc with a groove and a label in the accent, in the middle a hole.
  void record(lv_obj_t *parent, int x, int y, int d) {
    disc_ = ui::block(parent, theme::TRACK);
    lv_obj_set_style_radius(disc_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_size(disc_, d, d);
    lv_obj_set_pos(disc_, x, y);
    const int groove = d * 72 / 100, label = d * 34 / 100, hole = std::max(2, d * 4 / 100);
    ring_ = ui::block(disc_, theme::TRACK);
    lv_obj_set_style_radius(ring_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(ring_, std::max(1, ui::px(1)), 0);
    lv_obj_set_style_border_color(ring_, ui::color(theme::LINE), 0);
    lv_obj_set_size(ring_, groove, groove);
    lv_obj_set_pos(ring_, (d - groove) / 2, (d - groove) / 2);
    middle_ = ui::block(disc_, theme::ACCENT);
    lv_obj_set_style_radius(middle_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_size(middle_, label, label);
    lv_obj_set_pos(middle_, (d - label) / 2, (d - label) / 2);
    dot_ = ui::block(middle_, theme::CAMERA_PAGE);
    lv_obj_set_style_radius(dot_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_size(dot_, hole, hole);
    lv_obj_set_pos(dot_, (label - hole) / 2, (label - hole) / 2);
  }

  // The look changed: every colour of the record again.
  void paint_record() {
    ui::set_fill(disc_, tessera::Tone::NORMAL, theme::TRACK);
    ui::set_fill(ring_, tessera::Tone::NORMAL, theme::TRACK);
    lv_obj_set_style_border_color(ring_, ui::color(theme::LINE), 0);
    ui::set_fill(middle_, tessera::Tone::NORMAL, theme::ACCENT);
    ui::set_fill(dot_, tessera::Tone::NORMAL, theme::CAMERA_PAGE);
  }

  // Which keys the player has, where they stand, and the face of each: only positions, texts and colours change here.
  void paint_keys() {
    const bool play = keys_on_ && (features_ & (PAUSE | PLAY));
    const bool volume = keys_on_ && (features_ & (VOLUME_SET | VOLUME_STEP));
    const bool mute = keys_on_ && (features_ & VOLUME_MUTE);
    const bool shown[KEYS] = {mute, volume, play, volume};
    const uint32_t icons[KEYS] = {ICON_MUTED, ICON_VOLUME_DOWN, playing_ ? ICON_PAUSE : ICON_PLAY, ICON_VOLUME_UP};
    const int count = (mute ? 1 : 0) + (play ? 1 : 0) + (volume ? 2 : 0);
    const int row = count * key_ + (count > 1 ? (count - 1) * gap_ : 0);
    int x = keys_w_ ? keys_x_ + (keys_w_ - row) / 2 : keys_x_;
    for (int i = 0; i < KEYS; ++i) {
      key_shown_[i] = shown[i];
      if (!shown[i]) {
        lv_obj_add_flag(keys_[i], LV_OBJ_FLAG_HIDDEN);
        continue;
      }
      lv_obj_set_pos(keys_[i], x, keys_y_);
      key_left_[i] = x;
      lv_obj_remove_flag(keys_[i], LV_OBJ_FLAG_HIDDEN);
      // The mute key stands in the accent while the player is muted.
      const bool lit = i == MUTE && muted_;
      ui::set_fill(keys_[i], tessera::Tone::NORMAL, lit ? theme::ACCENT : theme::RAISED);
      lv_obj_t *icon = lv_obj_get_child(keys_[i], 0);
      ui::set_color(icon, lit ? theme::ON_ACCENT : theme::INK);
      ui::set_text(icon, ui::icon(icons[i]));
      x += key_ + gap_;
    }
  }

  // The key under the finger (tap_point() is in the tile's own pixels), with the bit of room around it a key takes.
  Key key_at(tessera::Point p) const {
    const int slack = ui::px(6);
    for (int i = 0; i < KEYS; ++i) {
      if (!key_shown_[i]) continue;
      if (p.x >= key_left_[i] - slack && p.x <= key_left_[i] + key_ + slack && p.y >= keys_y_ - slack &&
          p.y <= keys_y_ + key_ + slack)
        return static_cast<Key>(i);
    }
    return NONE;
  }

  const tessera::Plugin *plugin_;
  bool keys_on_ = true, playing_ = false, muted_ = false;
  bool key_shown_[KEYS]{};
  uint32_t features_ = 0;
  int key_ = 0, gap_ = 0, keys_x_ = 0, keys_w_ = 0, keys_y_ = 0, key_left_[KEYS]{};
  char entity_[128] = "";
  lv_obj_t *disc_{}, *ring_{}, *middle_{}, *dot_{};
  lv_obj_t *sleeve_{}, *title_{}, *artist_{}, *keys_[KEYS]{};
};

void VinylTile::setup() {
  // The id is the tile's id in the manifest.
  add_tile("deck", [this]() { return new DeckTile(this); });
}

}  // namespace esphome::vinyl_tile
