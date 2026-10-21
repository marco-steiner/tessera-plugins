#include "vinyl_tile.h"
#include <algorithm>
#include <cstdio>
#include <string>

namespace esphome::vinyl_tile {

using tessera::Font;
namespace ui = tessera::ui;

// Home Assistant's MediaPlayerEntityFeature: the bits of `supported_features` that decide the keys.
constexpr uint32_t F_PAUSE = 1, F_SEEK = 2, F_PREVIOUS = 16, F_NEXT = 32,
                   F_TURN_ON = 128, F_TURN_OFF = 256, F_PLAY = 16384;
// Icons of Tessera's set (Material Design Icons).
constexpr uint32_t ICON_PLAY = 0xF040A, ICON_PAUSE = 0xF03E4, ICON_PREVIOUS = 0xF04AE, ICON_NEXT = 0xF04AD,
                   ICON_POWER = 0xF0425, ICON_SPEAKER = 0xF04C3;
// The keys.
enum Key : uint8_t { POWER, PREV, PLAY_PAUSE, NEXT, KEYS, NONE = KEYS };

// The tile of a media player that fills the screen, laid out as Tessera's own player card: the player's name in the
// middle at the top with the power key at the right, the cover as a record sleeve (the record peeking out at its
// right) in the middle, the title and the artist under it, the progress bar with the times at both ends, and the keys
// previous, play or pause and next.
//
// The cover is the player's own picture (entity_picture). Play, pause and the power go through tessera::wish, so the
// key shows its other face at once and Home Assistant squares it; the other keys and the seek are actions. A tap on the
// progress bar jumps to that moment of the track.
class DeckTile : public tessera::Tile {
 public:
  explicit DeckTile(const tessera::Plugin *plugin) : plugin_(plugin) {}

  void create(const tessera::TileContext &c) override {
    keys_on_ = c.options["keys"] | true;
    std::snprintf(entity_, sizeof(entity_), "%s", c.entity);
    // The layout of the player card is drawn on a screen 476 high: u(n) is n of those pixels on this tile.
    auto u = [&](int n) { return n * c.height / 476; };
    const int w = c.width, m = u(20);
    const int power = std::max(ui::mm(10), u(60)), small = std::max(ui::mm(10), u(52)), big = std::max(ui::mm(12), u(64));
    const int title_h = ui::line_height(Font::HEADLINE), artist_h = ui::line_height(Font::BODY);

    // The top: the player's name in the middle, the power key at the right.
    const int top = u(12);
    name_ = ui::label(c.parent, Font::BODY_LARGE, theme::INK);
    lv_obj_set_width(name_, w - 2 * (power + m));
    lv_obj_set_pos(name_, power + m, top + (power - ui::line_height(Font::BODY_LARGE)) / 2);
    lv_obj_set_style_text_align(name_, LV_TEXT_ALIGN_CENTER, 0);
    key_left_[POWER] = w - m - power;
    key_top_[POWER] = top;
    key_size_[POWER] = power;

    // The cover in the middle, the record peeking out at its right.
    const int side = std::min(u(130), w * 45 / 100), x = (w - side) / 2, y = u(82);
    record(c.parent, x + side * 30 / 100, y + side * 4 / 100, side * 92 / 100);
    // The sleeve, made after the record so it lies over it. A board without pictures shows the speaker alone.
    if (ui::pictures()) {
      sleeve_ = ui::picture(c.parent);
      lv_obj_set_size(sleeve_, side, side);
      lv_obj_set_pos(sleeve_, x, y);
    }
    speaker_ = ui::label(c.parent, Font::ICON, theme::MUTED);
    lv_obj_set_width(speaker_, side);
    lv_obj_set_style_text_align(speaker_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(speaker_, x, y + (side - ui::line_height(Font::ICON)) / 2);
    ui::set_text(speaker_, ui::icon(ICON_SPEAKER));

    // The title and, under it, the artist.
    const int title_y = u(222);
    title_ = ui::label(c.parent, Font::HEADLINE, theme::INK);
    lv_obj_set_width(title_, w - 2 * m);
    lv_obj_set_pos(title_, m, title_y);
    lv_obj_set_style_text_align(title_, LV_TEXT_ALIGN_CENTER, 0);
    artist_ = ui::label(c.parent, Font::BODY, theme::MUTED);
    lv_obj_set_width(artist_, w - 2 * m);
    lv_obj_set_pos(artist_, m, title_y + title_h);
    lv_obj_set_style_text_align(artist_, LV_TEXT_ALIGN_CENTER, 0);

    // The progress: the time at the left, the bar, the length at the right.
    const int gap = ui::mm(2), time_w = ui::text_width("00:00", Font::BODY);
    seek_y_ = u(302);
    bar_x_ = m + time_w + gap;
    bar_w_ = w - 2 * (m + time_w + gap);
    bar_h_ = std::max(ui::px(4), u(6));
    knob_ = std::max(ui::px(14), u(20));
    const int time_y = seek_y_ - ui::line_height(Font::BODY) / 2;
    time_ = ui::label(c.parent, Font::BODY, theme::MUTED);
    lv_obj_set_width(time_, time_w);
    lv_obj_set_pos(time_, m, time_y);
    length_ = ui::label(c.parent, Font::BODY, theme::MUTED);
    lv_obj_set_width(length_, time_w);
    lv_obj_set_pos(length_, w - m - time_w, time_y);
    lv_obj_set_style_text_align(length_, LV_TEXT_ALIGN_RIGHT, 0);
    track_ = ui::block(c.parent, theme::TRACK);
    lv_obj_set_style_radius(track_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_size(track_, bar_w_, bar_h_);
    lv_obj_set_pos(track_, bar_x_, seek_y_ - bar_h_ / 2);
    fill_ = ui::block(c.parent, theme::INK);
    lv_obj_set_style_radius(fill_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_size(fill_, 1, bar_h_);
    lv_obj_set_pos(fill_, bar_x_, seek_y_ - bar_h_ / 2);
    knob_obj_ = ui::block(c.parent, theme::INK);
    lv_obj_set_style_radius(knob_obj_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_size(knob_obj_, knob_, knob_);
    lv_obj_set_pos(knob_obj_, bar_x_ - knob_ / 2, seek_y_ - knob_ / 2);

    // The keys under it: previous, play or pause (the largest), next.
    const int cy = u(369), dx = (small + big) / 2 + u(20);
    key_size_[PREV] = small;
    key_size_[PLAY_PAUSE] = big;
    key_size_[NEXT] = small;
    key_left_[PREV] = w / 2 - dx - small / 2;
    key_left_[PLAY_PAUSE] = w / 2 - big / 2;
    key_left_[NEXT] = w / 2 + dx - small / 2;
    key_top_[PREV] = cy - small / 2;
    key_top_[PLAY_PAUSE] = cy - big / 2;
    key_top_[NEXT] = cy - small / 2;
    for (int i = 0; i < KEYS; ++i) {
      lv_obj_t *k = keys_[i] = ui::block(c.parent, theme::RAISED);
      lv_obj_set_style_radius(k, LV_RADIUS_CIRCLE, 0);
      lv_obj_set_size(k, key_size_[i], key_size_[i]);
      lv_obj_set_pos(k, key_left_[i], key_top_[i]);
      lv_obj_t *icon = ui::label(k, Font::ICON, theme::INK);
      lv_obj_set_width(icon, key_size_[i]);
      lv_obj_set_style_text_align(icon, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_set_pos(icon, 0, (key_size_[i] - ui::line_height(Font::ICON)) / 2);
      lv_obj_add_flag(k, LV_OBJ_FLAG_HIDDEN);
    }
    ui::set_text(title_, plugin_->text("choose"));
    ui::set_text(time_, "0:00");
    ui::set_text(length_, "0:00");
  }

  // What the entity says: its state and the attributes the manifest names. Before a person has chosen a player there
  // is nothing, and the tile says so.
  void on_state(JsonObjectConst data) override {
    const std::string state = data["state"] | "";
    JsonObjectConst a = data["attributes"];
    const bool has_player = !state.empty();
    const bool idle = state == "idle" || state == "off" || state == "standby" || state == "unavailable" ||
                      state == "unknown";
    const bool was_playing = playing_;
    playing_ = state == "playing";
    on_ = has_player && state != "off" && state != "unavailable" && state != "unknown";
    features_ = a["supported_features"] | 0u;
    const std::string title = a["media_title"] | "";
    const std::string cover = (has_player && !idle) ? std::string(a["entity_picture"] | "") : std::string();
    ui::set_text(name_, data["name"] | "");
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
    // The player's own cover; the speaker stands in while there is none.
    if (sleeve_) ui::set_picture(sleeve_, cover.c_str(), tessera::Fit::COVER);
    if (cover.empty()) lv_obj_remove_flag(speaker_, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(speaker_, LV_OBJ_FLAG_HIDDEN);
    // The track's place: the position Home Assistant knew at a moment, which the tick carries on while it plays.
    const float position = idle ? 0.0f : (a["media_position"] | 0.0f);
    const uint32_t at = a["media_position_updated_at"] | 0u;
    duration_ = idle ? 0.0f : (a["media_duration"] | 0.0f);
    if (at) {
      pos_at_ = at;
    } else if (position != pos_ || was_playing != playing_) {
      pos_at_ = tessera::epoch();
    }
    pos_ = position;
    paint_keys();
    paint_progress(tessera::epoch());
  }

  void on_tick(uint32_t epoch) override { paint_progress(epoch); }

  void on_theme() override {
    ui::set_color(name_, theme::INK);
    ui::set_color(title_, theme::INK);
    ui::set_color(artist_, theme::MUTED);
    ui::set_color(speaker_, theme::MUTED);
    ui::set_color(time_, theme::MUTED);
    ui::set_color(length_, theme::MUTED);
    ui::set_fill(track_, tessera::Tone::NORMAL, theme::TRACK);
    ui::set_fill(fill_, tessera::Tone::NORMAL, theme::INK);
    ui::set_fill(knob_obj_, tessera::Tone::NORMAL, theme::INK);
    paint_record();
    paint_keys();
  }

  void on_tap() override {
    if (!entity_[0]) return;
    const tessera::Point p = tessera::tap_point();
    const Key key = key_at(p);
    if (key == PLAY_PAUSE) {
      tessera::wish(entity_, tessera::Wish::PLAYING, playing_ ? "0" : "1");
    } else if (key == POWER) {
      tessera::wish(entity_, tessera::Wish::ON_OFF, on_ ? "off" : "on");
    } else if (key == PREV) {
      tessera::action("media_player.media_previous_track", entity_);
    } else if (key == NEXT) {
      tessera::action("media_player.media_next_track", entity_);
    } else if (keys_on_ && (features_ & F_SEEK) && duration_ > 0 && p.x >= bar_x_ - ui::mm(3) &&
               p.x <= bar_x_ + bar_w_ + ui::mm(3) && p.y >= seek_y_ - ui::mm(6) && p.y <= seek_y_ + ui::mm(6)) {
      // A tap on the bar: the track goes to that place, and the bar shows it at once.
      const float frac = std::min(1.0f, std::max(0.0f, float(p.x - bar_x_) / float(bar_w_)));
      JsonDocument d;
      d["seek_position"] = frac * duration_;
      if (tessera::action("media_player.media_seek", entity_, d)) {
        pos_ = frac * duration_;
        pos_at_ = tessera::epoch();
        paint_progress(pos_at_);
      }
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

  // Which keys the player has and the face of each: only visibility, texts and colours change here.
  void paint_keys() {
    const bool shown[KEYS] = {keys_on_ && (features_ & (F_TURN_ON | F_TURN_OFF)),
                          keys_on_ && (features_ & F_PREVIOUS),
                          keys_on_ && (features_ & (F_PAUSE | F_PLAY)),
                          keys_on_ && (features_ & F_NEXT)};
    const uint32_t icons[KEYS] = {ICON_POWER, ICON_PREVIOUS, playing_ ? ICON_PAUSE : ICON_PLAY, ICON_NEXT};
    for (int i = 0; i < KEYS; ++i) {
      key_shown_[i] = shown[i];
      if (!shown[i]) {
        lv_obj_add_flag(keys_[i], LV_OBJ_FLAG_HIDDEN);
        continue;
      }
      lv_obj_remove_flag(keys_[i], LV_OBJ_FLAG_HIDDEN);
      // Play or pause is the one that stands out: the light circle with a dark icon.
      const bool lead = i == PLAY_PAUSE;
      ui::set_fill(keys_[i], tessera::Tone::NORMAL, lead ? theme::INK : theme::RAISED);
      lv_obj_t *icon = lv_obj_get_child(keys_[i], 0);
      ui::set_color(icon, lead ? theme::CARD : theme::INK);
      ui::set_text(icon, ui::icon(icons[i]));
    }
  }

  // The bar and the two times: the position carried on by the clock while it plays.
  void paint_progress(uint32_t epoch) {
    float pos = pos_;
    if (playing_ && pos_at_ && epoch > pos_at_) pos += float(epoch - pos_at_);
    if (duration_ > 0 && pos > duration_) pos = duration_;
    if (pos < 0) pos = 0;
    const int fw = duration_ > 0 ? int(pos / duration_ * float(bar_w_)) : 0;
    if (fw != last_fw_) {
      last_fw_ = fw;
      lv_obj_set_width(fill_, std::max(1, fw));
      lv_obj_set_x(knob_obj_, bar_x_ + fw - knob_ / 2);
    }
    char text[16];
    clock(text, sizeof(text), pos);
    ui::set_text(time_, text);
    clock(text, sizeof(text), duration_);
    ui::set_text(length_, text);
  }

  static void clock(char *out, size_t size, float seconds) {
    const int s = int(seconds);
    if (s >= 3600) std::snprintf(out, size, "%d:%02d:%02d", s / 3600, (s / 60) % 60, s % 60);
    else std::snprintf(out, size, "%d:%02d", s / 60, s % 60);
  }

  // The key under the finger (tap_point() is in the tile's own pixels), with the bit of room around it a key takes.
  Key key_at(tessera::Point p) const {
    const int slack = ui::px(6);
    for (int i = 0; i < KEYS; ++i) {
      if (!key_shown_[i]) continue;
      if (p.x >= key_left_[i] - slack && p.x <= key_left_[i] + key_size_[i] + slack && p.y >= key_top_[i] - slack &&
          p.y <= key_top_[i] + key_size_[i] + slack)
        return static_cast<Key>(i);
    }
    return NONE;
  }

  const tessera::Plugin *plugin_;
  bool keys_on_ = true, playing_ = false, on_ = false;
  bool key_shown_[KEYS]{};
  uint32_t features_ = 0, pos_at_ = 0;
  float pos_ = 0, duration_ = 0;
  int bar_x_ = 0, bar_w_ = 0, bar_h_ = 0, knob_ = 0, seek_y_ = 0, last_fw_ = -1;
  int key_left_[KEYS]{}, key_top_[KEYS]{}, key_size_[KEYS]{};
  char entity_[128] = "";
  lv_obj_t *disc_{}, *ring_{}, *middle_{}, *dot_{};
  lv_obj_t *sleeve_{}, *speaker_{}, *name_{}, *title_{}, *artist_{}, *time_{}, *length_{};
  lv_obj_t *track_{}, *fill_{}, *knob_obj_{}, *keys_[KEYS]{};
};

void VinylTile::setup() {
  // The id is the tile's id in the manifest.
  add_tile("deck", [this]() { return new DeckTile(this); });
}

}  // namespace esphome::vinyl_tile
