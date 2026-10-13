// ScreenManager's Video screens (ADR 0031): the film list is an ordinary
// list (ScreenManager.cpp); this is the player. Like Circuit, nothing here
// is drawn by LVGL while a film is open -- VideoPresenter owns the panel,
// and LVGL keeps one invisible object over the screen that takes the taps.

#include <lvgl.h>

#include "GameScreenStyle.h"
#include "ScreenManager.h"
#include "St77916Driver.h"
#include "TextFont.h"
#include "VideoPresenter.h"

namespace drehklang::ui {

using navigation::Screen;
using navigation::ScreenKind;

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

  videoSurface_ = lv_obj_create(screen_);
  lv_obj_set_size(videoSurface_, drivers::kLcdHorRes, drivers::kLcdVerRes);
  lv_obj_set_pos(videoSurface_, 0, 0);
  lv_obj_set_style_bg_opa(videoSurface_, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(videoSurface_, 0, 0);
  lv_obj_clear_flag(videoSurface_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(videoSurface_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(videoSurface_, onVideoTapped, LV_EVENT_CLICKED, this);

  videoTitle_ = tabs_.activeStack().current().params.folderPath;
  if (videoPresenter_ && videoPresenter_->open(videoTitle_, filmPosition(videoTitle_), true)) {
    return;
  }
  // Drawn by LVGL: the presenter never took the panel.
  videoTitle_.clear();
  lv_obj_t *label = lv_label_create(screen_);
  lv_obj_set_width(label, 240);
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(label, &drehklang_text_font_20, 0);
  lv_obj_set_style_text_color(label, lv_color_white(), 0);
  lv_label_set_text(label, "Can't play this film\nSee the README");
  lv_obj_center(label);
}

// Leaving, for whatever reason: the place is kept and the panel goes back.
void ScreenManager::leaveVideo() {
  if (videoPresenter_ && videoPresenter_->isOpen()) {
    saveFilmPosition(videoPresenter_->position());
    videoPresenter_->close();
  }
  videoSurface_ = nullptr;
  videoTitle_.clear();
}

void ScreenManager::onVideoTapped(lv_event_t *e) {
  auto *self = static_cast<ScreenManager *>(lv_event_get_user_data(e));
  if (!self || !self->videoSurface_ || !self->videoPresenter_) return;
  if (!self->videoPresenter_->isOpen()) return;
  self->videoPresenter_->togglePause();
  if (self->videoPresenter_->state() == VideoPresenter::State::Paused) {
    self->saveFilmPosition(self->videoPresenter_->position());
  }
}

bool ScreenManager::tickVideo(uint32_t /*nowMs*/, bool visible) {
  if (!videoSurface_ || !videoPresenter_ || videoTitle_.empty()) return false;
  if (tabs_.activeStack().current().kind != ScreenKind::VideoPlayer) return false;
  // Locked or dark: the lock screen is LVGL's to draw. The film stops where
  // it is and waits there, picture shown, for a tap.
  if (!visible) {
    if (videoPresenter_->isOpen()) {
      saveFilmPosition(videoPresenter_->position());
      videoPresenter_->close();
    }
    return false;
  }
  if (!videoPresenter_->isOpen() &&
      !videoPresenter_->open(videoTitle_, filmPosition(videoTitle_), false)) {
    videoTitle_.clear();
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
