// ScreenManager's Video player (ADR 0031); the film list is an ordinary
// list (ScreenManager.cpp).
//
// Playing, the film owns the panel (VideoPresenter) and LVGL keeps one
// invisible object over the screen that takes the tap which pauses. Paused,
// the panel is LVGL's again and the controls are Now Playing's: the still
// picture, Play, and the time pill -- held and turned, it jumps a minute
// per detent and plays on from there when let go (user, 2026-10-13).

#include <lvgl.h>

#include <algorithm>
#include <cstdio>

#include "GameScreenStyle.h"
#include "IconFont.h"
#include "LvglButtonHelpers.h"
#include "ScreenManager.h"
#include "St77916Driver.h"
#include "TextFont.h"
#include "VideoPresenter.h"

namespace drehklang::ui {

using navigation::Screen;
using navigation::ScreenKind;

namespace {

constexpr lv_coord_t kVideoPillW = 216;
constexpr lv_coord_t kVideoPillH = 28;
constexpr lv_coord_t kVideoTitleY = 52;
constexpr ui_widgets::MessageAnchor kVideoHintAnchor{drivers::kLcdHorRes / 2, 78};

void formatClock(char *out, size_t size, uint32_t seconds) {
  std::snprintf(out, size, "%u:%02u:%02u", static_cast<unsigned>(seconds / 3600),
                static_cast<unsigned>(seconds / 60 % 60), static_cast<unsigned>(seconds % 60));
}

}  // namespace

// From the list: music pauses first, like for a game -- the film's sound
// borrows the decoder, and the track comes back where it was.
void ScreenManager::openFilm(const std::string &title) {
  if (playback_.state() == playback::PlaybackState::Playing) {
    playback_.togglePlayPause(lv_tick_get());
  }
  navigation::ScreenParams params;
  params.folderPath = title;
  tabs_.activeStack().push(Screen{ScreenKind::VideoPlayer, params});
  render();
}

void ScreenManager::renderVideoPlayer() {
  lv_obj_set_style_bg_color(screen_, game_style::vacuum(), 0);
  lv_obj_set_style_bg_opa(screen_, LV_OPA_COVER, 0);
  videoTitle_ = tabs_.activeStack().current().params.folderPath;
  videoPill_ = nullptr;
  videoSeeking_ = false;

  if (!videoPresenter_ ||
      (!videoPresenter_->isOpen() &&
       !videoPresenter_->open(videoTitle_, filmPosition(videoTitle_), true))) {
    renderFilmError();
    return;
  }
  // Always there while the player is: tickVideo() looks for it.
  videoSurface_ = lv_obj_create(screen_);
  lv_obj_set_size(videoSurface_, drivers::kLcdHorRes, drivers::kLcdVerRes);
  lv_obj_set_pos(videoSurface_, 0, 0);
  lv_obj_set_style_bg_opa(videoSurface_, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(videoSurface_, 0, 0);
  lv_obj_clear_flag(videoSurface_, LV_OBJ_FLAG_SCROLLABLE);
  if (videoPresenter_->state() == VideoPresenter::State::Playing) {
    lv_obj_add_flag(videoSurface_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(videoSurface_, onVideoTapped, LV_EVENT_CLICKED, this);
    return;
  }
  lv_obj_clear_flag(videoSurface_, LV_OBJ_FLAG_CLICKABLE);
  renderFilmControls();
}

// Paused or cued: the still picture where it played, the title above it,
// Play over it, and the time pill below it.
void ScreenManager::renderFilmControls() {
  const lv_img_dsc_t *still = videoPresenter_->still();
  lv_coord_t pictureBottom = drivers::kLcdVerRes / 2;
  if (still) {
    lv_obj_t *picture = lv_img_create(screen_);
    lv_img_set_src(picture, still);
    lv_obj_center(picture);
    pictureBottom = (drivers::kLcdVerRes + static_cast<lv_coord_t>(still->header.h)) / 2;
  }

  lv_obj_t *title = lv_label_create(screen_);
  lv_obj_set_size(title, 220, 20);  // A fixed height, or LONG_DOT just wraps.
  lv_obj_set_style_text_font(title, &drehklang_text_font_16, 0);
  lv_obj_set_style_text_color(title, lv_color_white(), 0);
  lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
  lv_label_set_text(title, videoTitle_.c_str());
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, kVideoTitleY);

  makeIconButton(screen_, LV_SYMBOL_PLAY, 72, 72, LV_ALIGN_CENTER, 0, 0,
                 &ScreenManager::onFilmPlayClicked, this, ButtonRole::Primary,
                 &drehklang_text_font_28);

  videoPill_ = makeHoldButton(screen_, "", kVideoPillW, kVideoPillH, LV_ALIGN_TOP_MID, 0,
                              pictureBottom + 10, &ScreenManager::onFilmPillPressed,
                              &ScreenManager::onFilmPillReleased, this, ButtonRole::Secondary,
                              &drehklang_icon_font_16);
  lv_obj_set_ext_click_area(videoPill_, 0);
  showFilmTime(videoPresenter_->seconds());
}

void ScreenManager::renderFilmError() {
  videoTitle_.clear();
  lv_obj_t *label = lv_label_create(screen_);
  lv_obj_set_width(label, 240);
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(label, &drehklang_text_font_20, 0);
  lv_obj_set_style_text_color(label, lv_color_white(), 0);
  lv_label_set_text(label, "Can't play this film\nSee the README");
  lv_obj_center(label);
  if (haptics_) haptics_->feel(haptics::Feel::Refusal, millis());
}

void ScreenManager::showFilmTime(uint32_t seconds) {
  if (!videoPill_) return;
  char now[12];
  char total[12];
  formatClock(now, sizeof(now), seconds);
  formatClock(total, sizeof(total), videoPresenter_->durationSeconds());
  char text[48];
  std::snprintf(text, sizeof(text),
                DREHKLANG_ICON_FAST_REWIND " %s / %s " DREHKLANG_ICON_FAST_FORWARD, now, total);
  lv_label_set_text(lv_obj_get_child(videoPill_, 0), text);
}

// Leaving, for whatever reason: the place is kept and the panel goes back.
void ScreenManager::leaveVideo() {
  if (videoPresenter_ && videoPresenter_->isOpen()) {
    saveFilmPosition(videoPresenter_->position());
    videoPresenter_->close();
  }
  videoSurface_ = nullptr;
  videoPill_ = nullptr;
  videoSeeking_ = false;
  videoTitle_.clear();
}

void ScreenManager::onVideoTapped(lv_event_t *e) {
  auto *self = static_cast<ScreenManager *>(lv_event_get_user_data(e));
  if (!self || !self->videoPresenter_ || !self->videoPresenter_->isOpen()) return;
  self->videoPresenter_->pause();
  self->saveFilmPosition(self->videoPresenter_->position());
  self->render();
}

void ScreenManager::onFilmPlayClicked(lv_event_t *e) {
  auto *self = static_cast<ScreenManager *>(lv_event_get_user_data(e));
  if (!self || !self->videoPresenter_) return;
  self->videoPresenter_->resume();
  self->render();
}

// Holding the pill turns the knob from volume into minutes; say so, as Now
// Playing's does.
void ScreenManager::onFilmPillPressed(lv_event_t *e) {
  auto *self = static_cast<ScreenManager *>(lv_event_get_user_data(e));
  if (!self || !self->videoPresenter_) return;
  self->videoSeeking_ = true;
  self->videoSeekTarget_ = self->videoPresenter_->seconds();
  self->messages_.show("Turn knob: a minute per step", kVideoHintAnchor, millis(),
                       kShuttleHintMs);
}

void ScreenManager::onFilmPillReleased(lv_event_t *e) {
  auto *self = static_cast<ScreenManager *>(lv_event_get_user_data(e));
  if (!self || !self->videoSeeking_) return;
  self->videoSeeking_ = false;
  self->messages_.hide();
  if (self->videoSeekTarget_ == self->videoPresenter_->seconds()) return;
  self->videoPresenter_->jumpTo(self->videoSeekTarget_);
  self->saveFilmPosition(self->videoPresenter_->position());
  self->render();
}

// The knob is the volume, and a minute per detent while the pill is held.
void ScreenManager::onVideoKnob(int16_t delta) {
  if (!videoSeeking_ || !videoPresenter_) {
    playback_.adjustVolume(delta, millis());
    return;
  }
  const int64_t last =
      videoPresenter_->durationSeconds() > 0 ? videoPresenter_->durationSeconds() - 1 : 0;
  const int64_t target = static_cast<int64_t>(videoSeekTarget_) + delta * 60;
  videoSeekTarget_ = static_cast<uint32_t>(std::clamp<int64_t>(target, 0, last));
  messages_.hide();
  showFilmTime(videoSeekTarget_);
}

bool ScreenManager::tickVideo(uint32_t /*nowMs*/, bool visible) {
  if (!videoSurface_ || !videoPresenter_ || videoTitle_.empty()) return false;
  if (tabs_.activeStack().current().kind != ScreenKind::VideoPlayer) return false;
  // Locked or dark: the lock screen is LVGL's to draw. The film stops where
  // it is and waits there, paused, when the screen comes back.
  if (!visible) {
    if (videoPresenter_->isOpen()) {
      saveFilmPosition(videoPresenter_->position());
      videoPresenter_->close();
    }
    return false;
  }
  if (!videoPresenter_->isOpen()) {
    if (!videoPresenter_->open(videoTitle_, filmPosition(videoTitle_), false)) {
      videoTitle_.clear();
      return false;
    }
    render();
    return false;
  }
  if (!videoPresenter_->tick()) {
    // The end: next time it starts from the beginning.
    videoPresenter_->close();
    saveFilmPosition(video::VideoResume{});
    tabs_.back();
    render();
    return false;
  }
  return videoPresenter_->state() == VideoPresenter::State::Playing;
}

void ScreenManager::saveFilmPosition(const video::VideoResume &resume) {
  if (videoTitle_.empty()) return;
  settings_.setU32(video::soundKey(videoTitle_), resume.soundPosition);
  settings_.setU32(video::frameKey(videoTitle_), resume.frame);
}

video::VideoResume ScreenManager::filmPosition(const std::string &title) {
  video::VideoResume resume;
  settings_.getU32(video::soundKey(title), resume.soundPosition);
  settings_.getU32(video::frameKey(title), resume.frame);
  return resume;
}

}  // namespace drehklang::ui
