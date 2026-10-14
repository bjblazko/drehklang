// ScreenManager's dictaphone screens (ADR 0032): the recorder and one
// recording playing; the list between them is the generic one.
//
// Both are laid out like the tone generator (ADR 0024), top to bottom and
// centred: back and caption, the band (scope or spectrum, swiped
// between), what its scales are, which page it is on, the time, and a row
// with the primary button in the middle and a secondary one either side
// (on the recorder the right one only while recording).

#include <lvgl.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "IconFont.h"
#include "LvglButtonHelpers.h"
#include "ScreenManager.h"
#include "St77916Driver.h"
#include "TextFont.h"
#include "Theme.h"

namespace drehklang::ui {

using navigation::Screen;
using navigation::ScreenKind;

namespace {

// The tone generator's band and rows, so the two read as one family.
constexpr lv_coord_t kBandWidth = 260;
constexpr lv_coord_t kBandHeight = 88;
constexpr lv_coord_t kBandY = 76;
constexpr lv_coord_t kScaleLabelY = kBandY + kBandHeight + 4;
constexpr lv_coord_t kDotsY = kScaleLabelY + 22;
constexpr lv_coord_t kTimeY = 200;
constexpr lv_coord_t kNameY = 238;
// The row sits as high as the name allows: its outer buttons must stay
// inside the round glass.
constexpr lv_coord_t kPrimarySize = 72;
constexpr lv_coord_t kPrimaryY = 264;
constexpr lv_coord_t kSecondarySize = 48;
constexpr lv_coord_t kSecondaryX = 84;
constexpr lv_coord_t kSecondaryY = kPrimaryY + (kPrimarySize - kSecondarySize) / 2;
constexpr uint32_t kRewindMs = 5000;
constexpr lv_coord_t kRecordDotSize = 22;
constexpr lv_coord_t kResumeDotSize = 16;
constexpr lv_coord_t kRunDotSize = 10;
constexpr lv_coord_t kSwipeMinPx = 40;
// A voice has no pitch to lock on to; the 20 ms timebase the tone
// generator uses for noise shows a few syllables' worth of wave.
constexpr float kScopeTimebaseHz = 100.0f;

constexpr lv_coord_t kDialogQuestionY = 120;
constexpr lv_coord_t kDialogButtonY = 190;
constexpr lv_coord_t kDialogButtonWidth = 108;
constexpr lv_coord_t kDialogButtonHeight = 44;
constexpr lv_coord_t kDialogButtonGap = 12;

constexpr ui_widgets::MessageAnchor kCentre{drivers::kLcdHorRes / 2, drivers::kLcdVerRes / 2};

lv_obj_t *makeDot(lv_obj_t *parent, lv_coord_t size, lv_color_t color) {
  lv_obj_t *dot = lv_obj_create(parent);
  lv_obj_remove_style_all(dot);
  lv_obj_set_size(dot, size, size);
  lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(dot, color, 0);
  lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
  lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
  return dot;
}

lv_obj_t *makeTextButton(lv_obj_t *parent, const char *text, lv_coord_t x, lv_coord_t y,
                         lv_event_cb_t cb, void *userData) {
  lv_obj_t *btn = lv_btn_create(parent);
  lv_obj_set_size(btn, kDialogButtonWidth, kDialogButtonHeight);
  lv_obj_set_pos(btn, x, y);
  lv_obj_t *label = lv_label_create(btn);
  lv_obj_set_style_text_font(label, &drehklang_text_font_16, 0);
  lv_label_set_text(label, text);
  lv_obj_center(label);
  lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, userData);
  return btn;
}

}  // namespace

// What the dictaphone holds on to has to let go once neither of its two
// screens shows: the microphone (a recording is saved), the open file,
// and the band's buffers. The recorder keeps the player too, while it
// plays back the recording just made.
void ScreenManager::leaveDictaphoneScreens(ScreenKind next) {
  if (next != ScreenKind::Dictaphone) {
    if (recorder_) recorder_->close();
    lastRecordingPath_.clear();
  }
  if (next != ScreenKind::Recording && next != ScreenKind::Dictaphone && recordingPlayer_ &&
      !openRecordingPath_.empty()) {
    recordingPlayer_->close();
    openRecordingPath_.clear();
  }
  if (next != ScreenKind::Dictaphone && next != ScreenKind::Recording) {
    dictaphoneBand_.release();
  }
}

lv_obj_t *ScreenManager::makeDictaphoneBand() {
  lv_obj_t *band = dictaphoneBand_.create(screen_, kBandWidth, kBandHeight, theme::ink(),
                                          theme::surfaceAlt());
  lv_obj_align(band, LV_ALIGN_TOP_MID, 0, kBandY);
  lv_obj_add_flag(band, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(band, onDictaphoneBandPressed, LV_EVENT_PRESSED, this);
  lv_obj_add_event_cb(band, onDictaphoneBandReleased, LV_EVENT_RELEASED, this);
  lv_obj_add_event_cb(band, onDictaphoneBandReleased, LV_EVENT_PRESS_LOST, this);
  lastDictaphoneFrameMs_ = lv_tick_get();
  return band;
}

void ScreenManager::makeDictaphoneLabels() {
  dictaphoneScaleLabel_ = lv_label_create(screen_);
  lv_obj_set_style_text_font(dictaphoneScaleLabel_, &drehklang_text_font_14, 0);
  lv_obj_set_style_text_color(dictaphoneScaleLabel_, theme::structure(), 0);
  lv_label_set_text(dictaphoneScaleLabel_, "");
  lv_obj_align(dictaphoneScaleLabel_, LV_ALIGN_TOP_MID, 0, kScaleLabelY);

  dictaphoneDots_.create(screen_, 2, kDotsY, theme::ink(), theme::surfaceAlt());

  dictaphoneTimeLabel_ = lv_label_create(screen_);
  lv_obj_set_style_text_font(dictaphoneTimeLabel_, &drehklang_text_font_28, 0);
  lv_obj_set_style_text_color(dictaphoneTimeLabel_, theme::ink(), 0);
  lv_label_set_text(dictaphoneTimeLabel_, "");
  lv_obj_align(dictaphoneTimeLabel_, LV_ALIGN_TOP_MID, 0, kTimeY);
  shownDictaphoneSecond_ = UINT32_MAX;
  applyDictaphoneView();
}

void ScreenManager::applyDictaphoneView() {
  dictaphoneBand_.setView(dictaphoneBand_.view());
  dictaphoneDots_.setCurrent(dictaphoneBand_.view() == SignalBand::View::Scope ? 0 : 1);
  applyDictaphoneLabel();
}

// Only the label: setView() would clear the trace just drawn.
void ScreenManager::applyDictaphoneLabel() {
  if (!dictaphoneScaleLabel_) return;
  char text[40];
  dictaphoneBand_.label(text, sizeof(text));
  if (strcmp(lv_label_get_text(dictaphoneScaleLabel_), text) != 0) {
    lv_label_set_text(dictaphoneScaleLabel_, text);
  }
}

// --- The recorder ---

void ScreenManager::renderDictaphone() {
  makeDictaphoneBand();
  makeDictaphoneLabels();

  // The yellow of time passing (ADR 0008), next to the time while it runs.
  dictaphoneRunDot_ = makeDot(screen_, kRunDotSize, theme::time());

  dictaphoneNameLabel_ = lv_label_create(screen_);
  lv_obj_set_style_text_font(dictaphoneNameLabel_, &drehklang_text_font_16, 0);
  lv_obj_set_style_text_color(dictaphoneNameLabel_, theme::structure(), 0);
  lv_obj_align(dictaphoneNameLabel_, LV_ALIGN_TOP_MID, 0, kNameY);

  makeIconButton(screen_, LV_SYMBOL_LIST, kSecondarySize, kSecondarySize, LV_ALIGN_TOP_MID,
                 -kSecondaryX, kSecondaryY, onRecordingsClicked, this, ButtonRole::Secondary);
  dictaphonePrimary_ = makeIconButton(screen_, LV_SYMBOL_STOP, kPrimarySize, kPrimarySize,
                                      LV_ALIGN_TOP_MID, 0, kPrimaryY, onRecordClicked, this,
                                      ButtonRole::Primary);
  // Record is a dot, the way every tape deck draws it.
  lv_obj_center(makeDot(dictaphonePrimary_, kRecordDotSize, theme::surface()));
  // While recording: Pause, and while paused the dot again (go on
  // recording). After a recording: Play/Pause, to hear it back right here.
  dictaphoneSecondary_ = makeIconButton(screen_, LV_SYMBOL_PAUSE, kSecondarySize,
                                        kSecondarySize, LV_ALIGN_TOP_MID, kSecondaryX,
                                        kSecondaryY, onRecorderSideClicked, this,
                                        ButtonRole::Secondary);
  lv_obj_center(makeDot(dictaphoneSecondary_, kResumeDotSize, theme::ink()));

  if (recorder_ && !recorder_->recording() && !reviewing()) {
    // Read again on every visit: the card may have changed meanwhile.
    recordings_ = dictaphone::recordingsIn(directoryReader_);
    nextRecordingNumber_ = dictaphone::nextRecordingNumber(recordings_);
    // The microphone takes I2S0 from the player, which parks the track;
    // paused first, as the tone generator does, so the UI agrees.
    if (playback_.state() == playback::PlaybackState::Playing) {
      playback_.togglePlayPause(lv_tick_get());
    }
    if (!recorder_->listen()) refuse("Microphone unavailable", kCentre);
  }
  shownRecording_ = recorder_ && recorder_->recording();
  applyRecordButton();
}

// Record (a dot) or Stop, the time's dot, and the name: the next
// recording's while idle, this one's while it runs.
void ScreenManager::applyRecordButton() {
  if (!dictaphonePrimary_) return;
  const bool recording = recorder_ && recorder_->recording();
  const bool paused = recording && recorder_->paused();
  // A recording that just ended, however it ended, took its number.
  if (shownRecording_ && !recording) ++nextRecordingNumber_;
  shownRecording_ = recording;
  shownRecordingPaused_ = paused;
  const bool reviewPlaying = reviewing() && recordingPlayer_->playing();
  shownRecordingPlaying_ = reviewPlaying;
  if (dictaphoneSecondary_) {
    // Only where it does something (ux-guidelines §7).
    if (recording || !lastRecordingPath_.empty()) {
      lv_obj_clear_flag(dictaphoneSecondary_, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(dictaphoneSecondary_, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_t *glyph = lv_obj_get_child(dictaphoneSecondary_, 0);
    lv_obj_t *resumeDot = lv_obj_get_child(dictaphoneSecondary_, 1);
    const bool showDot = recording && paused;
    if (showDot) {
      lv_obj_add_flag(glyph, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(resumeDot, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_clear_flag(glyph, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(resumeDot, LV_OBJ_FLAG_HIDDEN);
      lv_label_set_text(glyph, recording || reviewPlaying ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    }
  }

  lv_obj_t *glyph = lv_obj_get_child(dictaphonePrimary_, 0);
  lv_obj_t *dot = lv_obj_get_child(dictaphonePrimary_, 1);
  if (recording) {
    lv_obj_clear_flag(glyph, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(glyph, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_HIDDEN);
  }
  if (dictaphoneNameLabel_) {
    // The one playing back, else the one being made or made next.
    const int number = reviewing() ? lastRecordingNumber_ : nextRecordingNumber_;
    lv_label_set_text(dictaphoneNameLabel_, dictaphone::recordingTitle(number).c_str());
  }
  shownDictaphoneSecond_ = UINT32_MAX;
  updateRecorderTime();
}

void ScreenManager::updateRecorderTime() {
  if (!dictaphoneTimeLabel_ || !recorder_) return;
  if (reviewing()) {
    if (dictaphoneRunDot_) lv_obj_add_flag(dictaphoneRunDot_, LV_OBJ_FLAG_HIDDEN);
    updateRecordingTime();
    return;
  }
  const bool recording = recorder_->recording();
  const uint32_t ms = recording ? recorder_->recordedMs() : 0;
  if (ms / 1000 == shownDictaphoneSecond_) return;
  shownDictaphoneSecond_ = ms / 1000;
  char text[16];
  dictaphone::formatDuration(text, sizeof(text), ms);
  lv_label_set_text(dictaphoneTimeLabel_, text);
  if (!dictaphoneRunDot_) return;
  // Yellow means time passing; paused, it stands still.
  if (recording && !recorder_->paused()) {
    lv_obj_clear_flag(dictaphoneRunDot_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_update_layout(dictaphoneTimeLabel_);
    lv_obj_align_to(dictaphoneRunDot_, dictaphoneTimeLabel_, LV_ALIGN_OUT_LEFT_MID, -10, 0);
  } else {
    lv_obj_add_flag(dictaphoneRunDot_, LV_OBJ_FLAG_HIDDEN);
  }
}

void ScreenManager::onRecordClicked(lv_event_t *e) {
  auto *self = static_cast<ScreenManager *>(lv_event_get_user_data(e));
  if (!self || !self->recorder_) return;
  dictaphone::Recorder &recorder = *self->recorder_;
  if (recorder.recording()) {
    recorder.stopRecording();
  } else {
    // A new recording ends the playback of the last one.
    self->endReview(false);
    if (!recorder.listen()) {
      self->refuse("Microphone unavailable", kCentre);
    } else {
      const std::string path = dictaphone::recordingPath(
          dictaphone::recordingFileName(self->nextRecordingNumber_));
      if (recorder.record(path)) {
        self->lastRecordingPath_ = path;
        self->lastRecordingNumber_ = self->nextRecordingNumber_;
      } else {
        self->refuse("Cannot write to the SD card", kCentre);
      }
    }
  }
  self->applyRecordButton();
}

// Pause/resume while recording; afterwards, play back what was just made.
void ScreenManager::onRecorderSideClicked(lv_event_t *e) {
  auto *self = static_cast<ScreenManager *>(lv_event_get_user_data(e));
  if (!self || !self->recorder_) return;
  dictaphone::Recorder &recorder = *self->recorder_;
  if (recorder.recording()) {
    if (recorder.paused()) {
      recorder.resumeRecording();
    } else {
      recorder.pauseRecording();
    }
  } else if (self->reviewing()) {
    dictaphone::RecordingPlayer &player = *self->recordingPlayer_;
    if (player.playing()) {
      player.pause();
    } else {
      player.play();
    }
  } else {
    self->startReview();
  }
  self->applyRecordButton();
}

// The player takes I2S0 from the microphone; the band follows it.
void ScreenManager::startReview() {
  if (!recordingPlayer_ || lastRecordingPath_.empty()) return;
  // Its header is patched a moment after Stop (SdWavWriter).
  if (recorder_->saving()) {
    refuse("Saving recording", kCentre);
    return;
  }
  if (!recordingPlayer_->open(lastRecordingPath_)) {
    refuse("Cannot play this file", kCentre);
    return;
  }
  openRecordingPath_ = lastRecordingPath_;
  recordingPlayer_->play();
  shownDictaphoneSecond_ = UINT32_MAX;
}

// Back to the microphone: at the end of the playback, or before a new
// recording (which listens by itself).
void ScreenManager::endReview(bool listen) {
  if (!reviewing()) return;
  recordingPlayer_->close();
  openRecordingPath_.clear();
  shownDictaphoneSecond_ = UINT32_MAX;
  if (listen && recorder_ && !recorder_->listen()) {
    refuse("Microphone unavailable", kCentre);
  }
}

bool ScreenManager::reviewing() const {
  return recordingPlayer_ && !openRecordingPath_.empty() &&
         tabs_.activeStack().current().kind == ScreenKind::Dictaphone;
}

void ScreenManager::onRecordingsClicked(lv_event_t *e) {
  auto *self = static_cast<ScreenManager *>(lv_event_get_user_data(e));
  if (!self) return;
  self->tabs_.activeStack().push(Screen{ScreenKind::Recordings, {}});
  self->render();
}

// --- One recording ---

void ScreenManager::openRecording(size_t index) {
  if (index >= recordings_.size()) return;
  navigation::ScreenParams params;
  params.folderPath = recordings_[index].fileName;
  params.row = static_cast<uint16_t>(recordings_[index].number);
  tabs_.activeStack().push(Screen{ScreenKind::Recording, params});
  render();
}

void ScreenManager::renderRecording() {
  const std::string path =
      dictaphone::recordingPath(tabs_.activeStack().current().params.folderPath);
  if (recordingPlayer_ && openRecordingPath_ != path) {
    if (recordingPlayer_->open(path)) {
      openRecordingPath_ = path;
    } else {
      openRecordingPath_.clear();
      refuse("Cannot play this file", kCentre);
    }
  }

  makeDictaphoneBand();
  makeDictaphoneLabels();
  makeIconButton(screen_, LV_SYMBOL_TRASH, kSecondarySize, kSecondarySize, LV_ALIGN_TOP_MID,
                 -kSecondaryX, kSecondaryY, onRecordingDeleteClicked, this,
                 ButtonRole::Secondary);
  dictaphonePrimary_ = makeIconButton(screen_, LV_SYMBOL_PLAY, kPrimarySize, kPrimarySize,
                                      LV_ALIGN_TOP_MID, 0, kPrimaryY, onRecordingPlayClicked,
                                      this, ButtonRole::Primary);
  makeIconButton(screen_, DREHKLANG_ICON_REPLAY_5, kSecondarySize, kSecondarySize,
                 LV_ALIGN_TOP_MID, kSecondaryX, kSecondaryY, onRecordingRewindClicked, this,
                 ButtonRole::Secondary, &drehklang_icon_font_28);
  applyRecordingPlayButton();
}

void ScreenManager::applyRecordingPlayButton() {
  if (!dictaphonePrimary_ || !recordingPlayer_) return;
  shownRecordingPlaying_ = recordingPlayer_->playing();
  lv_label_set_text(lv_obj_get_child(dictaphonePrimary_, 0),
                    shownRecordingPlaying_ ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
  shownDictaphoneSecond_ = UINT32_MAX;
  updateRecordingTime();
}

void ScreenManager::updateRecordingTime() {
  if (!dictaphoneTimeLabel_ || !recordingPlayer_) return;
  const uint32_t position = recordingPlayer_->positionMs();
  if (position / 1000 == shownDictaphoneSecond_) return;
  shownDictaphoneSecond_ = position / 1000;
  char now[12];
  char total[12];
  dictaphone::formatDuration(now, sizeof(now), position);
  dictaphone::formatDuration(total, sizeof(total), recordingPlayer_->durationMs());
  char text[28];
  snprintf(text, sizeof(text), "%s / %s", now, total);
  lv_label_set_text(dictaphoneTimeLabel_, text);
}

void ScreenManager::onRecordingPlayClicked(lv_event_t *e) {
  auto *self = static_cast<ScreenManager *>(lv_event_get_user_data(e));
  if (!self || !self->recordingPlayer_ || self->openRecordingPath_.empty()) return;
  dictaphone::RecordingPlayer &player = *self->recordingPlayer_;
  if (player.playing()) {
    player.pause();
  } else {
    // A recording plays alone, like a tone: the track is paused and
    // waits to be resumed.
    if (self->playback_.state() == playback::PlaybackState::Playing) {
      self->playback_.togglePlayPause(lv_tick_get());
    }
    player.play();
  }
  self->applyRecordingPlayButton();
}

void ScreenManager::onRecordingRewindClicked(lv_event_t *e) {
  auto *self = static_cast<ScreenManager *>(lv_event_get_user_data(e));
  if (!self || !self->recordingPlayer_ || self->openRecordingPath_.empty()) return;
  self->recordingPlayer_->rewind(kRewindMs);
  self->shownDictaphoneSecond_ = UINT32_MAX;
  self->updateRecordingTime();
}

void ScreenManager::onRecordingDeleteClicked(lv_event_t *e) {
  auto *self = static_cast<ScreenManager *>(lv_event_get_user_data(e));
  if (self && !self->openRecordingPath_.empty()) self->showDeleteDialog();
}

// Deleting cannot be undone, so it asks first, in words, with the
// recording's name: a modal layer over the screen, Cancel beside Delete.
void ScreenManager::showDeleteDialog() {
  if (deleteDialog_) return;
  if (recordingPlayer_ && recordingPlayer_->playing()) {
    recordingPlayer_->pause();
    applyRecordingPlayButton();
  }
  deleteDialog_ = lv_obj_create(screen_);
  lv_obj_remove_style_all(deleteDialog_);
  lv_obj_set_size(deleteDialog_, drivers::kLcdHorRes, drivers::kLcdVerRes);
  lv_obj_set_style_bg_color(deleteDialog_, theme::surface(), 0);
  lv_obj_set_style_bg_opa(deleteDialog_, LV_OPA_COVER, 0);
  lv_obj_clear_flag(deleteDialog_, LV_OBJ_FLAG_SCROLLABLE);
  // Takes every tap, so nothing underneath reacts while it shows.
  lv_obj_add_flag(deleteDialog_, LV_OBJ_FLAG_CLICKABLE);

  const std::string question =
      "Delete " +
      dictaphone::recordingTitle(tabs_.activeStack().current().params.row) + "?";
  lv_obj_t *label = lv_label_create(deleteDialog_);
  lv_obj_set_style_text_font(label, &drehklang_text_font_20, 0);
  lv_obj_set_style_text_color(label, theme::ink(), 0);
  lv_label_set_text(label, question.c_str());
  lv_obj_align(label, LV_ALIGN_TOP_MID, 0, kDialogQuestionY);

  const lv_coord_t left =
      (drivers::kLcdHorRes - 2 * kDialogButtonWidth - kDialogButtonGap) / 2;
  lv_obj_t *cancel =
      makeTextButton(deleteDialog_, "Cancel", left, kDialogButtonY, onDeleteCancelled, this);
  theme::styleSecondaryButton(cancel);
  lv_obj_set_style_radius(cancel, kDialogButtonHeight / 2, 0);
  lv_obj_t *confirm = makeTextButton(deleteDialog_, "Delete",
                                     left + kDialogButtonWidth + kDialogButtonGap,
                                     kDialogButtonY, onDeleteConfirmed, this);
  // Red: this one needs attention (ADR 0008's signal colours).
  theme::stylePrimaryButton(confirm);
  lv_obj_set_style_bg_color(confirm, theme::warning(), 0);
  lv_obj_set_style_radius(confirm, kDialogButtonHeight / 2, 0);
}

void ScreenManager::onDeleteCancelled(lv_event_t *e) {
  auto *self = static_cast<ScreenManager *>(lv_event_get_user_data(e));
  if (!self || !self->deleteDialog_) return;
  // From inside its own button's click: deleted after the event is done.
  lv_obj_del_async(self->deleteDialog_);
  self->deleteDialog_ = nullptr;
}

void ScreenManager::onDeleteConfirmed(lv_event_t *e) {
  auto *self = static_cast<ScreenManager *>(lv_event_get_user_data(e));
  if (!self || !self->recordingPlayer_) return;
  const std::string path = self->openRecordingPath_;
  self->openRecordingPath_.clear();
  if (!self->recordingPlayer_->remove(path)) {
    self->refuse("Could not delete", kCentre);
  }
  self->tabs_.back();
  self->render();
}

// --- Shared ---

void ScreenManager::onDictaphoneBandPressed(lv_event_t *e) {
  auto *self = static_cast<ScreenManager *>(lv_event_get_user_data(e));
  lv_point_t point;
  lv_indev_get_point(lv_indev_get_act(), &point);
  if (self) self->dictaphoneSwipeStartX_ = point.x;
}

// Right to left brings the spectrum in, left to right takes it back out,
// as on the tone generator.
void ScreenManager::onDictaphoneBandReleased(lv_event_t *e) {
  auto *self = static_cast<ScreenManager *>(lv_event_get_user_data(e));
  if (!self) return;
  lv_point_t point;
  lv_indev_get_point(lv_indev_get_act(), &point);
  const lv_coord_t dx = point.x - self->dictaphoneSwipeStartX_;
  SignalBand::View next = self->dictaphoneBand_.view();
  if (dx <= -kSwipeMinPx) next = SignalBand::View::Spectrum;
  if (dx >= kSwipeMinPx) next = SignalBand::View::Scope;
  if (next == self->dictaphoneBand_.view()) return;
  self->dictaphoneBand_.setView(next);
  self->applyDictaphoneView();
}

void ScreenManager::drawDictaphoneBand(signal::SampleSource &source, uint32_t dtMs) {
  constexpr uint32_t kRate = 16000;
  const size_t count = source.readRecent(dictaphoneBand_.buffer(), dictaphoneBand_.capacity());
  if (dictaphoneBand_.view() == SignalBand::View::Spectrum) {
    dictaphoneBand_.showSpectrum(count, kRate, dtMs);
    return;
  }
  if (count == 0) return;
  // No set level, unlike a tone: the range follows the loudest sample.
  const int levelDb = SignalBand::peakDb(dictaphoneBand_.buffer(), count);
  dictaphoneBand_.showScope(count, kRate, kScopeTimebaseHz, levelDb);
  applyDictaphoneLabel();
}

void ScreenManager::tickDictaphone(uint32_t nowMs, bool visible) {
  const ScreenKind kind = tabs_.activeStack().current().kind;
  signal::SampleSource *source = nullptr;
  if (kind == ScreenKind::Dictaphone && recorder_ && dictaphonePrimary_) {
    if (recorder_->takeWriteFailure()) refuse("SD card full\nRecording saved", kCentre);
    // Played to the end: the microphone again.
    if (reviewing() && !recordingPlayer_->playing() &&
        recordingPlayer_->positionMs() >= recordingPlayer_->durationMs()) {
      endReview(true);
      applyRecordButton();
    }
    if (shownRecording_ != recorder_->recording() ||
        shownRecordingPaused_ != recorder_->paused() ||
        shownRecordingPlaying_ != (reviewing() && recordingPlayer_->playing())) {
      applyRecordButton();
    }
    if (visible) updateRecorderTime();
    source = reviewing() ? static_cast<signal::SampleSource *>(recordingPlayer_) : recorder_;
  } else if (kind == ScreenKind::Recording && recordingPlayer_ && dictaphonePrimary_) {
    if (shownRecordingPlaying_ != recordingPlayer_->playing()) applyRecordingPlayButton();
    if (visible) updateRecordingTime();
    source = recordingPlayer_;
  }
  if (!source || deleteDialog_) return;
  if (!visible) {
    lastDictaphoneFrameMs_ = nowMs;
    return;
  }
  const uint32_t dtMs = nowMs - lastDictaphoneFrameMs_;
  if (dtMs < kToneScopeFrameMs) return;
  lastDictaphoneFrameMs_ = nowMs;
  drawDictaphoneBand(*source, std::min<uint32_t>(dtMs, 100));
}

}  // namespace drehklang::ui
