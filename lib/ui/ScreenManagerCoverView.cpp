// ScreenManager's full-screen cover (ADR 0033): a tap on Now Playing's
// cover shows only the cover, as large as the round glass allows, on a
// blurred copy of itself. A second tap adds the time and the title in the
// two segments the square leaves free above and below; a third goes back.

#include <lvgl.h>

#include <cstdlib>

#include "CoverBackdrop.h"
#include "ScreenHelpers.h"
#include "ScreenManager.h"
#include "SquareResampler.h"
#include "St77916Driver.h"
#include "TagReader.h"
#include "TextFont.h"
#include "Theme.h"

#ifdef DREHKLANG_COVER_DEBUG
#include <Arduino.h>
#endif

namespace drehklang::ui {

namespace {

// The largest square inside the 360 px circle: its corners touch the edge.
constexpr uint16_t kViewCoverSize = 254;
constexpr uint16_t kBackdropSize = drivers::kLcdHorRes;
// On the 96 px cached cover: only the colours are left (ADR 0033).
constexpr int kBlurRadius = 8;
constexpr int kBlurPasses = 3;

// The segments above and below the square are (360 - 254) / 2 = 53 px
// tall; a pill sits in the middle of each, narrow enough for the circle.
// (Not kTimePillY: ScreenManager has one of its own for Now Playing, and
// inside a member function the class's name wins.)
constexpr lv_coord_t kPillHeight = 30;
constexpr lv_coord_t kInfoTimeY = 14;
constexpr lv_coord_t kInfoTitleY = drivers::kLcdVerRes - 14 - kPillHeight;
constexpr lv_coord_t kTitleMaxWidth = 150;
constexpr lv_coord_t kPillPadX = 14;

void describe(lv_img_dsc_t &dsc, const std::vector<uint16_t> &pixels, uint16_t size) {
  dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
  dsc.header.always_zero = 0;
  dsc.header.w = size;
  dsc.header.h = size;
  dsc.data_size = static_cast<uint32_t>(pixels.size() * sizeof(uint16_t));
  dsc.data = reinterpret_cast<const uint8_t *>(pixels.data());
}

lv_obj_t *makePill(lv_obj_t *parent, lv_coord_t y, const lv_font_t *font) {
  lv_obj_t *pill = lv_obj_create(parent);
  lv_obj_remove_style_all(pill);
  lv_obj_set_size(pill, LV_SIZE_CONTENT, kPillHeight);
  lv_obj_set_style_bg_color(pill, theme::surface(), 0);
  lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(pill, kPillHeight / 2, 0);
  lv_obj_set_style_pad_hor(pill, kPillPadX, 0);
  lv_obj_clear_flag(pill, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(pill, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_align(pill, LV_ALIGN_TOP_MID, 0, y);
  lv_obj_t *label = lv_label_create(pill);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_color(label, theme::ink(), 0);
  lv_obj_center(label);
  return label;
}

}  // namespace

void ScreenManager::openCoverView() {
  coverView_ = CoverView::Cover;
  render();
}

bool ScreenManager::closeCoverView() {
  if (coverView_ == CoverView::Closed) return false;
  forgetCoverView();
  render();
  return true;
}

// Gives the ~390 KB of PSRAM back.
void ScreenManager::forgetCoverView() {
  coverView_ = CoverView::Closed;
  coverViewAlbum_.clear();
  std::vector<uint16_t>().swap(coverViewPixels_);
  std::vector<uint16_t>().swap(coverViewBackdrop_);
}

// The cover decoded again from the original at full size, and the
// backdrop blurred from the cached one. Done once per album: a re-render
// (play/pause, the info tap) keeps both.
bool ScreenManager::loadCoverView(const std::string &album) {
#ifdef DREHKLANG_COVER_DEBUG
  const uint32_t startMs = millis();
#endif
  uint16_t smallSize = 0;
  std::vector<uint16_t> small;
  if (!coverReader_.loadCover(album, &smallSize, &small) || smallSize == 0) return false;

  bool sharp = false;
#ifdef DREHKLANG_COVER_DEBUG
  uint32_t readMs = 0, decodeMs = 0, blurMs = 0;
#endif
  if (auto file = fileOpener_.open(playback_.currentPath())) {
    const auto tags = library::TagReader::read(*file, playback_.currentPath());
    std::vector<uint8_t> jpeg;
    const bool found = library::CoverArtCache::readCoverJpeg(album, *file, tags,
                                                             directoryReader_, fileOpener_, &jpeg);
#ifdef DREHKLANG_COVER_DEBUG
    readMs = millis() - startMs;
#endif
    sharp = found && jpegDecoder_.decodeSquare(jpeg.data(), jpeg.size(), kViewCoverSize,
                                               &coverViewPixels_);
#ifdef DREHKLANG_COVER_DEBUG
    decodeMs = millis() - startMs - readMs;
    Serial.printf("[cover] jpeg %u bytes\n", static_cast<unsigned>(jpeg.size()));
#endif
  }
  // No original to be had: the cached one, enlarged (softly, bilinear).
  if (!sharp) {
    library::SquareResampler::resample(small.data(), smallSize, smallSize, kViewCoverSize,
                                       &coverViewPixels_);
  }

#ifdef DREHKLANG_COVER_DEBUG
  const uint32_t blurStartMs = millis();
#endif
  coverview::boxBlur(small.data(), smallSize, kBlurRadius, kBlurPasses);
#ifdef DREHKLANG_COVER_DEBUG
  blurMs = millis() - blurStartMs;
#endif
  // Only around the cover: it hides the middle.
  coverview::enlarge(small.data(), smallSize, kBackdropSize, &coverViewBackdrop_,
                     kViewCoverSize);
  coverViewAlbum_ = album;
#ifdef DREHKLANG_COVER_DEBUG
  Serial.printf("[cover] full-screen cover %s in %lu ms (read %lu, decode %lu, blur %lu)\n",
                sharp ? "decoded" : "enlarged", static_cast<unsigned long>(millis() - startMs),
                static_cast<unsigned long>(readMs), static_cast<unsigned long>(decodeMs),
                static_cast<unsigned long>(blurMs));
#endif
  return true;
}

void ScreenManager::renderCoverView() {
  const std::string album = library::CoverArtCache::albumFolderPathFor(playback_.currentPath());
  // A track from another album with no cover closes it.
  if (album != coverViewAlbum_ && !loadCoverView(album)) {
    forgetCoverView();
    render();
    return;
  }

  describe(coverViewBackdropDsc_, coverViewBackdrop_, kBackdropSize);
  lv_obj_t *backdrop = lv_img_create(screen_);
  lv_img_set_src(backdrop, &coverViewBackdropDsc_);
  lv_obj_set_pos(backdrop, 0, 0);

  describe(coverViewDsc_, coverViewPixels_, kViewCoverSize);
  lv_obj_t *cover = lv_img_create(screen_);
  lv_img_set_src(cover, &coverViewDsc_);
  lv_obj_center(cover);

  if (coverView_ == CoverView::Info) {
    coverViewTimeLabel_ = makePill(screen_, kInfoTimeY, &drehklang_text_font_16);
    coverViewShownSecond_ = -1;
    updateCoverViewTime();
    lv_obj_t *title = makePill(screen_, kInfoTitleY, &drehklang_text_font_16);
    const std::string text = trackInfoFor(playback_.currentPath()).title;
    lv_point_t size;
    lv_txt_get_size(&size, text.c_str(), &drehklang_text_font_16, 0, 0, LV_COORD_MAX,
                    LV_TEXT_FLAG_NONE);
    // Narrower than the circle down there; a long title ends in dots,
    // which needs a fixed width (AGENTS.md: LONG_DOT on a content-sized
    // label only wraps).
    if (size.x > kTitleMaxWidth) {
      lv_obj_set_width(title, kTitleMaxWidth);
      setClampedText(title, text.c_str(), 1);
    } else {
      lv_label_set_text(title, text.c_str());
    }
  }

  // Over everything: the whole screen is the one place to tap.
  lv_obj_t *tapArea = lv_obj_create(screen_);
  lv_obj_remove_style_all(tapArea);
  lv_obj_set_size(tapArea, drivers::kLcdHorRes, drivers::kLcdVerRes);
  lv_obj_add_flag(tapArea, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(tapArea, onCoverViewPressed, LV_EVENT_PRESSED, this);
  lv_obj_add_event_cb(tapArea, onCoverViewReleased, LV_EVENT_RELEASED, this);
}

void ScreenManager::updateCoverViewTime() {
  if (!coverViewTimeLabel_) return;
  const uint32_t seconds = playback_.elapsedMs(millis()) / 1000;
  if (static_cast<int32_t>(seconds) == coverViewShownSecond_) return;
  coverViewShownSecond_ = static_cast<int32_t>(seconds);
  const uint32_t total = playback_.durationSeconds();
  char text[24];
  if (total != 0) {
    snprintf(text, sizeof(text), "%u:%02u / %u:%02u", static_cast<unsigned>(seconds / 60),
             static_cast<unsigned>(seconds % 60), static_cast<unsigned>(total / 60),
             static_cast<unsigned>(total % 60));
  } else {
    snprintf(text, sizeof(text), "%u:%02u", static_cast<unsigned>(seconds / 60),
             static_cast<unsigned>(seconds % 60));
  }
  lv_label_set_text(coverViewTimeLabel_, text);
}

void ScreenManager::onCoverViewPressed(lv_event_t *e) {
  auto *self = static_cast<ScreenManager *>(lv_event_get_user_data(e));
  if (!self) return;
  lv_indev_get_point(lv_indev_get_act(), &self->coverViewPressPoint_);
}

// Cover, then cover with time and title, then back to the player. Only a
// tap: LVGL clicks after a drag too, and a swipe here is the way back
// (closeCoverView(), from the gesture).
void ScreenManager::onCoverViewReleased(lv_event_t *e) {
  constexpr lv_coord_t kTapSlop = 10;
  auto *self = static_cast<ScreenManager *>(lv_event_get_user_data(e));
  if (!self) return;
  lv_point_t point;
  lv_indev_get_point(lv_indev_get_act(), &point);
  if (std::abs(point.x - self->coverViewPressPoint_.x) > kTapSlop ||
      std::abs(point.y - self->coverViewPressPoint_.y) > kTapSlop) {
    return;
  }
  if (self->coverView_ == CoverView::Cover) {
    self->coverView_ = CoverView::Info;
    self->render();
  } else {
    self->closeCoverView();
  }
}

}  // namespace drehklang::ui
