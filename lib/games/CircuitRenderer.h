#pragma once

#include <cstdint>

#include "CircuitHud.h"
#include "CircuitScene.h"
#include "CircuitSprites.h"

namespace drehklang::games {

// Draws Circuit (ADR 0030): the classic segment-based pseudo-3D road,
// scaled sprites and the HUD, into RGB565 stripes for a 360x360 round
// panel. Pure C++ -- no LVGL, no Arduino -- so it is host-tested.
//
// prepare() projects one frame once (the road's centre and width per
// screen row, every sprite's rectangle); renderStripe() then fills any
// band of rows from that plan, so the render task can hand the stripes to
// the panel one after another while it draws the next. Only pixels inside
// the circle are ever written: the corners are not visible and not worth
// the bus time (~21 % of the square).
class CircuitRenderer {
 public:
  static constexpr int kSize = 360;
  static constexpr int kHorizon = 162;
  // Where the bottom of the player's car sits.
  static constexpr int kCarBottom = 344;

  struct Span {
    int16_t x0;
    int16_t width;
  };

  // The visible columns of a row, or of the widest row of a stripe.
  static Span rowSpan(int y);
  static Span stripeSpan(int y0, int rows);

  void prepare(const CircuitScene &scene);

  // Draws rows [y0, y0 + rows) into dst, which holds span.width pixels
  // per row starting at column span.x0. Pixels of dst outside the circle
  // are left as they were.
  void renderStripe(uint16_t *dst, int y0, int rows, Span span) const;

 private:
  struct Row {
    int16_t center;
    int16_t half;
    uint8_t band;
    bool road;
    bool startLine;
  };

  struct SpriteItem {
    const circuit_art::Sprite *sprite;
    int16_t x, y, w, h;
    int16_t clipY;
    uint8_t colours;  // which colour table: 0 = track, 1.. = livery, player
  };

  static constexpr int kDrawDistance = 160;
  static constexpr int kMaxSprites = 72;
  static constexpr int kPlayerColours = circuit_art::kLiveryCount + 1;

  void buildColours(const CircuitScene &scene);
  void project(const CircuitScene &scene);
  void addSprite(const circuit_art::Sprite &sprite, int32_t unitsPerPixel, float screenX,
                 float bottomY, float scale, int clipY, uint8_t colours);
  void addPlayer(const CircuitScene &scene);

  void drawBackground(uint16_t *row, int y, int x0, int x1) const;
  void drawRoad(uint16_t *row, const Row &plan, int x0, int x1) const;
  void drawSprite(const SpriteItem &item, uint16_t *dst, int y0, int rows, Span span) const;
  void drawRect(const CircuitHud::Rect &rect, uint16_t *dst, int y0, int rows, Span span) const;
  void drawTriangle(const CircuitHud::Triangle &triangle, uint16_t *dst, int y0, int rows,
                    Span span) const;
  void drawText(const CircuitHud::Text &text, uint16_t *dst, int y0, int rows, Span span) const;
  void drawGlyph(const uint8_t *glyph, int left, int top, int scale, uint16_t colour,
                 uint16_t *dst, int y0, int rows, Span span) const;

  const circuit_art::Palette *palette_ = nullptr;
  const circuit_art::Sprite *backdrop_ = nullptr;
  uint16_t colours_[kPlayerColours + 1][circuit_art::kPaletteSlots] = {};
  Row rows_[kSize] = {};
  int roadTop_ = kHorizon;
  int backdropOffset_ = 0;
  SpriteItem sprites_[kMaxSprites] = {};
  int spriteCount_ = 0;
  CircuitHud hud_;
};

}  // namespace drehklang::games
