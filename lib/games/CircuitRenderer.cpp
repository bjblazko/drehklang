#include "CircuitRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace drehklang::games {

namespace {

using circuit_art::Sprite;

// Camera: 1000 units above the road and 840 behind the car, which puts
// the road under the car exactly at kCarBottom. kDepth is 1 / tan(50 deg),
// a 100 degree field of view: wide enough that speed reads as speed.
constexpr float kDepth = 0.839f;
constexpr float kCameraHeight = 1000.0f;
constexpr int32_t kCameraBack = 840;
constexpr float kHalf = CircuitRenderer::kSize / 2.0f;
// Light and dark bands alternate every few segments, the oldest speed cue
// there is.
constexpr int32_t kBandSegments = 3;
// Pixels per sprite pixel are worked out from these: world units each
// sprite pixel stands for. The car is about a third of the road.
constexpr int32_t kCarUnits = 15;
constexpr int32_t kBannerUnits = 19;
// The backdrop scrolls by the road's heading.
constexpr int32_t kHeadingPerPixel = 2048;

struct Projected {
  float x;      // screen column of the road's centre
  float y;      // screen row
  float half;   // road half-width, pixels
  float scale;  // pixels per world unit / kHalf
};

const Sprite &scenerySprite(Scenery kind) {
  switch (kind) {
    case Scenery::Palm: return circuit_art::kPalm;
    case Scenery::Rock: return circuit_art::kRock;
    case Scenery::Cactus: return circuit_art::kCactus;
    case Scenery::Lamp: return circuit_art::kLamp;
    case Scenery::Sign: return circuit_art::kSign;
    default: return circuit_art::kTower;
  }
}

int32_t sceneryUnits(Scenery kind) {
  switch (kind) {
    case Scenery::Tower: return 30;
    case Scenery::Rock: return 18;
    default: return 20;
  }
}

int isqrt(int value) {
  int root = static_cast<int>(std::sqrt(static_cast<float>(value)));
  while (root * root > value) --root;
  while ((root + 1) * (root + 1) <= value) ++root;
  return root;
}

}  // namespace

CircuitRenderer::Span CircuitRenderer::rowSpan(int y) {
  const int dy = 2 * y + 1 - kSize;  // twice the distance from the centre
  const int inside = kSize * kSize - dy * dy;
  if (inside <= 0) return Span{static_cast<int16_t>(kSize / 2), 0};
  const int half = isqrt(inside) / 2;
  return Span{static_cast<int16_t>(kSize / 2 - half), static_cast<int16_t>(2 * half)};
}

CircuitRenderer::Span CircuitRenderer::stripeSpan(int y0, int rows) {
  Span widest{static_cast<int16_t>(kSize / 2), 0};
  for (int y = y0; y < y0 + rows; ++y) {
    const Span span = rowSpan(y);
    if (span.width > widest.width) widest = span;
  }
  return widest;
}

void CircuitRenderer::prepare(const CircuitScene &scene) {
  palette_ = &circuit_art::kPalettes[scene.trackIndex];
  backdrop_ = &circuit_art::kBackdrops[scene.trackIndex];
  const int32_t scroll = scene.heading / kHeadingPerPixel;
  backdropOffset_ = ((scroll % backdrop_->width) + backdrop_->width) % backdrop_->width;
  buildColours(scene);
  spriteCount_ = 0;
  project(scene);
  addPlayer(scene);
  hud_.layout(scene);
}

// One colour table per car paint: the track's own, each computer car's
// livery, and the player's with its brake lights lit or not.
void CircuitRenderer::buildColours(const CircuitScene &scene) {
  for (int table = 0; table <= kPlayerColours; ++table) {
    std::memcpy(colours_[table], palette_->slots, sizeof(palette_->slots));
  }
  for (int livery = 0; livery < circuit_art::kLiveryCount; ++livery) {
    colours_[1 + livery][circuit_art::kBodySlot] = circuit_art::kLiveries[livery][0];
    colours_[1 + livery][circuit_art::kStripeSlot] = circuit_art::kLiveries[livery][1];
  }
  // The tail lights burn when braking and glow dimly otherwise.
  constexpr uint8_t kTailSlot = 7;
  constexpr uint16_t kDimTail = 0x80C3;
  if (!scene.braking) colours_[kPlayerColours][kTailSlot] = kDimTail;
}

void CircuitRenderer::project(const CircuitScene &scene) {
  for (Row &row : rows_) row = Row{0, 0, 0, false, false};
  const CircuitTrack &track = *scene.track;
  constexpr int32_t kSegment = CircuitTrack::kSegmentLength;

  int32_t cameraZ = scene.z - kCameraBack;
  if (cameraZ < 0) cameraZ += track.length();
  const int32_t base = cameraZ / kSegment;
  const float fraction = static_cast<float>(cameraZ % kSegment) / kSegment;

  // Heights at each segment boundary, relative to the first; the camera
  // rides kCameraHeight above the road under the car.
  constexpr int kCarSegments = kCameraBack / kSegment;
  float roadUnderCar = 0.0f;
  {
    float y = 0.0f;
    for (int n = 0; n <= kCarSegments; ++n) {
      y += static_cast<float>(track.segment(base + n).slope) / CircuitTrack::kOne;
    }
    roadUnderCar = y;
  }
  const float cameraY = roadUnderCar + kCameraHeight;
  const float cameraX = static_cast<float>(scene.x);

  int maxY = kSize;
  float x = 0.0f;
  float y = -static_cast<float>(track.segment(base).slope) / CircuitTrack::kOne * fraction;
  float dx = -static_cast<float>(track.segment(base).curve) / CircuitTrack::kOne * fraction;
  Projected near{};
  bool haveNear = false;

  // Computer cars, by the segment they are in.
  int carSegment[CircuitTraffic::kMaxCars];
  float carFraction[CircuitTraffic::kMaxCars];
  for (int i = 0; i < scene.carCount; ++i) {
    int32_t ahead = scene.cars[i].z - cameraZ;
    if (ahead < 0) ahead += track.length();
    carSegment[i] = ahead / kSegment;
    carFraction[i] = static_cast<float>(ahead % kSegment) / kSegment;
  }

  for (int n = 0; n <= kDrawDistance; ++n) {
    const CircuitTrack::Segment segment = track.segment(base + n);
    const float z = (n - fraction) * kSegment;
    Projected far{};
    const bool visible = z > 1.0f;
    if (visible) {
      far.scale = kDepth / z;
      far.x = kHalf + far.scale * (x - cameraX) * kHalf;
      far.y = kHorizon - far.scale * (y - cameraY) * kHalf;
      far.half = far.scale * CircuitTrack::kRoadHalfWidth * kHalf;
    }

    if (haveNear && visible) {
      const int clip = maxY;
      // The stretch between the previous boundary and this one.
      const int top = std::max(0, static_cast<int>(std::ceil(far.y)));
      const int bottom = std::min(maxY, static_cast<int>(std::ceil(near.y)));
      const int index = track.wrap(base + n - 1);
      for (int row = top; row < bottom; ++row) {
        const float t = (row - far.y) / (near.y - far.y);
        Row &plan = rows_[row];
        plan.center = static_cast<int16_t>(far.x + (near.x - far.x) * t);
        plan.half = static_cast<int16_t>(far.half + (near.half - far.half) * t);
        plan.band = static_cast<uint8_t>((index / kBandSegments) & 1);
        plan.road = true;
        plan.startLine = index == 0;
      }
      if (top < maxY) maxY = std::max(top, 0);

      // What stands in the stretch, drawn later back to front.
      const CircuitTrack::Segment behind = track.segment(base + n - 1);
      if (behind.scenery != Scenery::None) {
        const Sprite &sprite = scenerySprite(behind.scenery);
        const float offset = static_cast<float>(CircuitTrack::sceneryOffset(behind.scenery));
        if (behind.side <= 0) {
          addSprite(sprite, sceneryUnits(behind.scenery), near.x - offset * near.scale * kHalf,
                    near.y, near.scale, clip, 0);
        }
        if (behind.side >= 0) {
          addSprite(sprite, sceneryUnits(behind.scenery), near.x + offset * near.scale * kHalf,
                    near.y, near.scale, clip, 0);
        }
      }
      if (index == track.def().checkpoint || index == 0) {
        addSprite(circuit_art::kBanner, kBannerUnits, near.x, near.y, near.scale, clip, 0);
      }
      for (int i = 0; i < scene.carCount; ++i) {
        if (carSegment[i] != n - 1) continue;
        const float t = carFraction[i];
        const float scale = near.scale + (far.scale - near.scale) * t;
        const float centre = near.x + (far.x - near.x) * t;
        const float bottomY = near.y + (far.y - near.y) * t;
        addSprite(circuit_art::kCarStraight, kCarUnits,
                  centre + static_cast<float>(scene.cars[i].x) * scale * kHalf, bottomY,
                  scale, clip, static_cast<uint8_t>(1 + scene.cars[i].livery));
      }
    }

    if (visible) {
      near = far;
      haveNear = true;
    }
    y += static_cast<float>(segment.slope) / CircuitTrack::kOne;
    x += dx;
    dx += static_cast<float>(segment.curve) / CircuitTrack::kOne;
  }
  roadTop_ = maxY;
}

// Collected front to back (nearest first) so that, when the list is full,
// it is the furthest that go missing.
void CircuitRenderer::addSprite(const Sprite &sprite, int32_t unitsPerPixel, float screenX,
                                float bottomY, float scale, int clipY, uint8_t colours) {
  // The last slot is the player's, whatever the scenery needs.
  const bool player = clipY == kSize;
  if (spriteCount_ >= (player ? kMaxSprites : kMaxSprites - 1)) return;
  const float pixel = scale * unitsPerPixel * kHalf;
  const int w = static_cast<int>(sprite.width * pixel);
  const int h = static_cast<int>(sprite.height * pixel);
  if (w < 1 || h < 1) return;
  const int left = static_cast<int>(screenX) - w / 2;
  const int top = static_cast<int>(bottomY) - h;
  if (top >= clipY || left >= kSize || left + w <= 0 || top + h <= 0) return;
  sprites_[spriteCount_++] = SpriteItem{&sprite,
                                        static_cast<int16_t>(left),
                                        static_cast<int16_t>(top),
                                        static_cast<int16_t>(w),
                                        static_cast<int16_t>(h),
                                        static_cast<int16_t>(clipY),
                                        colours};
}

// The player's car sits still at the bottom; the world moves. A crash
// shows the explosion instead, and the car shakes off the road.
void CircuitRenderer::addPlayer(const CircuitScene &scene) {
  const float scale = kDepth / kCameraBack;
  const int shake = (scene.offRoad && scene.speed > 0 && (scene.frame & 1)) ? 2 : 0;
  if (scene.crashLeftMs > 0) {
    const Sprite &frame = scene.crashLeftMs > 1100   ? circuit_art::kExplosion0
                          : scene.crashLeftMs > 600 ? circuit_art::kExplosion1
                                                    : circuit_art::kExplosion2;
    addSprite(frame, kCarUnits, kHalf, kCarBottom, scale, kSize, 0);
    return;
  }
  const int steer = CircuitGame::effectiveSteering(scene.steering);
  const Sprite &car = steer > 0   ? circuit_art::kCarRight
                      : steer < 0 ? circuit_art::kCarLeft
                                  : circuit_art::kCarStraight;
  addSprite(car, kCarUnits, kHalf, static_cast<float>(kCarBottom - shake), scale, kSize,
            kPlayerColours);
}

void CircuitRenderer::renderStripe(uint16_t *dst, int y0, int rows, Span span) const {
  for (int y = y0; y < y0 + rows; ++y) {
    const Span visible = rowSpan(y);
    const int x0 = std::max<int>(visible.x0, span.x0);
    const int x1 = std::min<int>(visible.x0 + visible.width, span.x0 + span.width);
    if (x0 >= x1) continue;
    uint16_t *row = dst + (y - y0) * span.width - span.x0;
    if (rows_[y].road) {
      drawRoad(row, rows_[y], x0, x1);
    } else {
      drawBackground(row, y, x0, x1);
    }
  }
  // Back to front: the list was gathered nearest first, and the player
  // was added last of all.
  for (int i = spriteCount_ - 2; i >= 0; --i) drawSprite(sprites_[i], dst, y0, rows, span);
  if (spriteCount_ > 0) drawSprite(sprites_[spriteCount_ - 1], dst, y0, rows, span);
  for (int i = 0; i < hud_.rectCount(); ++i) drawRect(hud_.rect(i), dst, y0, rows, span);
  for (int i = 0; i < hud_.textCount(); ++i) drawText(hud_.text(i), dst, y0, rows, span);
  if (hud_.hasMarker()) drawTriangle(hud_.marker(), dst, y0, rows, span);
}

// Sky in hard bands down to the horizon, the backdrop standing on it, and
// far-off ground below it wherever the road does not reach (over a crest).
void CircuitRenderer::drawBackground(uint16_t *row, int y, int x0, int x1) const {
  if (y >= kHorizon) {
    std::fill(row + x0, row + x1, palette_->grass[1]);
    return;
  }
  const uint16_t sky = palette_->sky[std::min(5, y * 6 / kHorizon)];
  const int backdropTop = kHorizon - backdrop_->height;
  if (y < backdropTop) {
    std::fill(row + x0, row + x1, sky);
    return;
  }
  const uint8_t *source = backdrop_->pixels + (y - backdropTop) * backdrop_->width;
  for (int x = x0; x < x1; ++x) {
    const uint8_t index = source[(x + backdropOffset_) % backdrop_->width];
    row[x] = index ? palette_->slots[index] : sky;
  }
}

void CircuitRenderer::drawRoad(uint16_t *row, const Row &plan, int x0, int x1) const {
  auto fill = [&](int from, int to, uint16_t colour) {
    from = std::max(from, x0);
    to = std::min(to, x1);
    if (from < to) std::fill(row + from, row + to, colour);
  };
  const int c = plan.center;
  const int h = plan.half;
  const int rumble = h / 6 + 1;
  fill(x0, x1, palette_->grass[plan.band]);
  fill(c - h - rumble, c + h + rumble, palette_->rumble[plan.band]);
  fill(c - h, c + h, palette_->road[plan.band]);
  if (plan.startLine) {
    // A chequered start line: squares sized to the road's width.
    const int square = std::max(1, h / 8);
    for (int x = std::max(c - h, x0); x < std::min(c + h, x1); ++x) {
      row[x] = (((x - (c - h)) / square) & 1) ? 0xFFFF : 0x0000;
    }
    return;
  }
  if (plan.band == 0) {
    const int lane = h / 32 + 1;
    const int marker = h * 275 / CircuitTrack::kRoadHalfWidth;
    fill(c - marker - lane, c - marker + lane, palette_->lane);
    fill(c + marker - lane, c + marker + lane, palette_->lane);
  }
}

void CircuitRenderer::drawSprite(const SpriteItem &item, uint16_t *dst, int y0, int rows,
                                 Span span) const {
  const int top = std::max<int>(item.y, y0);
  const int bottom = std::min<int>({item.y + item.h, y0 + rows, item.clipY});
  if (top >= bottom) return;
  const Sprite &sprite = *item.sprite;
  const uint16_t *colours = colours_[item.colours];
  const int32_t stepX = (static_cast<int32_t>(sprite.width) << 16) / item.w;
  for (int y = top; y < bottom; ++y) {
    const Span visible = rowSpan(y);
    const int x0 = std::max<int>({visible.x0, span.x0, item.x});
    const int x1 = std::min<int>({visible.x0 + visible.width, span.x0 + span.width,
                                  item.x + item.w});
    if (x0 >= x1) continue;
    const int sourceY = (y - item.y) * sprite.height / item.h;
    const uint8_t *source = sprite.pixels + sourceY * sprite.width;
    uint16_t *row = dst + (y - y0) * span.width - span.x0;
    int32_t u = (x0 - item.x) * stepX;
    for (int x = x0; x < x1; ++x, u += stepX) {
      const uint8_t index = source[u >> 16];
      if (index) row[x] = colours[index];
    }
  }
}

void CircuitRenderer::drawRect(const CircuitHud::Rect &rect, uint16_t *dst, int y0, int rows,
                               Span span) const {
  const int top = std::max<int>(rect.y, y0);
  const int bottom = std::min<int>(rect.y + rect.h, y0 + rows);
  for (int y = top; y < bottom; ++y) {
    const Span visible = rowSpan(y);
    const int x0 = std::max<int>({visible.x0, span.x0, rect.x});
    const int x1 = std::min<int>({visible.x0 + visible.width, span.x0 + span.width,
                                  rect.x + rect.w});
    if (x0 >= x1) continue;
    uint16_t *row = dst + (y - y0) * span.width - span.x0;
    std::fill(row + x0, row + x1, rect.color);
  }
}

// A filled triangle, row by row: where each row's centre line crosses the
// three edges, filled between the outermost crossings.
void CircuitRenderer::drawTriangle(const CircuitHud::Triangle &t, uint16_t *dst, int y0,
                                   int rows, Span span) const {
  const int top = std::max(y0, static_cast<int>(std::min({t.y[0], t.y[1], t.y[2]})));
  const int bottom =
      std::min(y0 + rows, static_cast<int>(std::max({t.y[0], t.y[1], t.y[2]})) + 1);
  for (int y = top; y < bottom; ++y) {
    const float cy = y + 0.5f;
    float left = kSize;
    float right = -1.0f;
    for (int e = 0; e < 3; ++e) {
      const float ax = t.x[e], ay = t.y[e];
      const float bx = t.x[(e + 1) % 3], by = t.y[(e + 1) % 3];
      if ((cy < ay) == (cy < by)) continue;
      const float x = ax + (bx - ax) * (cy - ay) / (by - ay);
      left = std::min(left, x);
      right = std::max(right, x);
    }
    if (right < left) continue;
    const Span visible = rowSpan(y);
    const int x0 = std::max({static_cast<int>(std::lround(left)), static_cast<int>(visible.x0),
                             static_cast<int>(span.x0)});
    const int x1 = std::min({static_cast<int>(std::lround(right)) + 1,
                             visible.x0 + visible.width, span.x0 + span.width});
    if (x0 >= x1) continue;
    uint16_t *row = dst + (y - y0) * span.width - span.x0;
    std::fill(row + x0, row + x1, t.color);
  }
}

// Shadowed, so it reads on sky, road and sprite alike.
void CircuitRenderer::drawText(const CircuitHud::Text &text, uint16_t *dst, int y0, int rows,
                               Span span) const {
  const int scale = text.scale;
  const int advance = 6 * scale;
  const int length = static_cast<int>(std::strlen(text.text));
  const int width = length * advance - scale;
  if (text.top + 8 * scale + scale <= y0 || text.top >= y0 + rows) return;
  const int left = text.centerX - width / 2;
  for (int pass = 0; pass < 2; ++pass) {
    const int offset = pass == 0 ? scale : 0;
    const uint16_t colour = pass == 0 ? 0x0000 : text.color;
    for (int i = 0; i < length; ++i) {
      const uint8_t *glyph = circuit_art::glyph(text.text[i]);
      if (glyph) {
        drawGlyph(glyph, left + i * advance + offset, text.top + offset, scale, colour, dst, y0,
                  rows, span);
      }
    }
  }
}

void CircuitRenderer::drawGlyph(const uint8_t *glyph, int left, int top, int scale,
                                uint16_t colour, uint16_t *dst, int y0, int rows,
                                Span span) const {
  for (int gy = 0; gy < 7; ++gy) {
    const uint8_t bits = glyph[gy];
    if (bits == 0) continue;
    for (int sy = 0; sy < scale; ++sy) {
      const int y = top + gy * scale + sy;
      if (y < y0 || y >= y0 + rows) continue;
      const Span visible = rowSpan(y);
      const int x0 = std::max<int>(visible.x0, span.x0);
      const int x1 = std::min<int>(visible.x0 + visible.width, span.x0 + span.width);
      uint16_t *row = dst + (y - y0) * span.width - span.x0;
      for (int gx = 0; gx < 5; ++gx) {
        if (!(bits & (0x10 >> gx))) continue;
        const int from = std::max(left + gx * scale, x0);
        const int to = std::min(left + gx * scale + scale, x1);
        if (from < to) std::fill(row + from, row + to, colour);
      }
    }
  }
}

}  // namespace drehklang::games
