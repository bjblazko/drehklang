#include <unity.h>

#include <cstdint>
#include <cstdlib>
#include <vector>

#include "CircuitGame.h"
#include "CircuitRenderer.h"
#include "CircuitScene.h"
#include "CircuitTrack.h"
#include "CircuitTracks.h"

using drehklang::games::CircuitGame;
using drehklang::games::CircuitRenderer;
using drehklang::games::CircuitScene;
using drehklang::games::Scenery;
using drehklang::games::TrackDef;
using drehklang::games::TrackSection;

void setUp() {}
void tearDown() {}

namespace {

constexpr int kSize = CircuitRenderer::kSize;
constexpr uint16_t kSentinel = 0x1234;

constexpr TrackSection kStraight[] = {{600, 0, 0, Scenery::None, 0, 0}};
constexpr TrackDef kStraightTrack{"STRAIGHT", kStraight, 1, 300, 60000, 20000, 0, 0};
constexpr TrackSection kRight[] = {{20, 0, 0, Scenery::None, 0, 0},
                                   {580, 6, 0, Scenery::None, 0, 0}};
constexpr TrackDef kRightTrack{"RIGHT", kRight, 2, 300, 60000, 20000, 0, 0};

// A race underway on `track`, a little way past the start line.
CircuitScene sceneOn(CircuitGame &game, const TrackDef &track, uint32_t driveMs) {
  game.select(track);
  game.startCountdown(0);
  for (uint32_t t = 5; t <= CircuitGame::kCountdownMs + driveMs; t += 5) game.tick(t);
  CircuitScene scene = CircuitScene::capture(game);
  return scene;
}

// The whole frame, stripe by stripe as the device draws it, into a
// buffer full of sentinels.
std::vector<uint16_t> renderFrame(CircuitRenderer &renderer, const CircuitScene &scene) {
  renderer.prepare(scene);
  std::vector<uint16_t> frame(kSize * kSize, kSentinel);
  constexpr int kRows = 16;
  std::vector<uint16_t> stripe(kSize * kRows);
  for (int y0 = 0; y0 < kSize; y0 += kRows) {
    const int rows = std::min(kRows, kSize - y0);
    const CircuitRenderer::Span span = CircuitRenderer::stripeSpan(y0, rows);
    std::fill(stripe.begin(), stripe.end(), kSentinel);
    renderer.renderStripe(stripe.data(), y0, rows, span);
    for (int r = 0; r < rows; ++r) {
      for (int x = 0; x < span.width; ++x) {
        frame[(y0 + r) * kSize + span.x0 + x] = stripe[r * span.width + x];
      }
    }
  }
  return frame;
}

bool insideCircle(int x, int y) {
  const int dx = 2 * x + 1 - kSize;
  const int dy = 2 * y + 1 - kSize;
  return dx * dx + dy * dy < kSize * kSize;
}

bool isRoad(uint16_t c, int track) {
  const auto &p = drehklang::games::circuit_art::kPalettes[track];
  return c == p.road[0] || c == p.road[1];
}

// Leftmost and rightmost road pixel of a row, or -1.
void roadExtent(const std::vector<uint16_t> &frame, int y, int &left, int &right) {
  left = right = -1;
  for (int x = 0; x < kSize; ++x) {
    if (!isRoad(frame[y * kSize + x], 0)) continue;
    if (left < 0) left = x;
    right = x;
  }
}

}  // namespace

void test_spans_follow_the_circle() {
  const CircuitRenderer::Span middle = CircuitRenderer::rowSpan(180);
  TEST_ASSERT_INT_WITHIN(2, 0, middle.x0);
  TEST_ASSERT_INT_WITHIN(2, kSize, middle.width);
  const CircuitRenderer::Span top = CircuitRenderer::rowSpan(0);
  TEST_ASSERT_LESS_THAN(60, top.width);
  const CircuitRenderer::Span stripe = CircuitRenderer::stripeSpan(0, 16);
  TEST_ASSERT_EQUAL(CircuitRenderer::rowSpan(15).width, stripe.width);
}

void test_nothing_is_written_outside_the_circle() {
  CircuitGame game;
  CircuitRenderer renderer;
  const auto frame = renderFrame(renderer, sceneOn(game, kStraightTrack, 2000));
  for (int y = 0; y < kSize; ++y) {
    for (int x = 0; x < kSize; ++x) {
      if (!insideCircle(x, y)) TEST_ASSERT_EQUAL_HEX16(kSentinel, frame[y * kSize + x]);
    }
  }
}

void test_everything_inside_the_circle_is_drawn() {
  CircuitGame game;
  CircuitRenderer renderer;
  const auto frame = renderFrame(renderer, sceneOn(game, kStraightTrack, 2000));
  for (int y = 2; y < kSize - 2; ++y) {
    TEST_ASSERT_NOT_EQUAL(kSentinel, frame[y * kSize + kSize / 2]);
  }
}

void test_a_straight_road_is_centred() {
  CircuitGame game;
  CircuitRenderer renderer;
  CircuitScene scene = sceneOn(game, kStraightTrack, 2000);
  scene.carCount = 0;
  const auto frame = renderFrame(renderer, scene);
  // Above the car, where the road is not covered by it.
  for (int y : {180, 200, 220}) {
    int left = 0;
    int right = 0;
    roadExtent(frame, y, left, right);
    TEST_ASSERT_TRUE(left > 0);
    TEST_ASSERT_INT_WITHIN(3, kSize - 1 - right, left);
  }
}

void test_the_road_narrows_toward_the_horizon() {
  CircuitGame game;
  CircuitRenderer renderer;
  CircuitScene scene = sceneOn(game, kStraightTrack, 2000);
  scene.carCount = 0;
  const auto frame = renderFrame(renderer, scene);
  int nearL, nearR, farL, farR;
  roadExtent(frame, 240, nearL, nearR);
  roadExtent(frame, 175, farL, farR);
  TEST_ASSERT_GREATER_THAN(farR - farL, nearR - nearL);
}

void test_a_right_bend_leads_the_road_right() {
  CircuitGame game;
  CircuitRenderer renderer;
  CircuitScene scene = sceneOn(game, kRightTrack, 1000);
  scene.carCount = 0;
  const auto frame = renderFrame(renderer, scene);
  int left, right;
  roadExtent(frame, 168, left, right);
  TEST_ASSERT_TRUE(left > 0);
  TEST_ASSERT_GREATER_THAN(kSize / 2 + 10, (left + right) / 2);
}

void test_the_player_car_is_drawn_at_the_bottom_centre() {
  CircuitGame game;
  CircuitRenderer renderer;
  const auto frame = renderFrame(renderer, sceneOn(game, kStraightTrack, 2000));
  const auto &body = drehklang::games::circuit_art::kPalettes[0].slots[2];
  int found = 0;
  for (int y = 280; y < CircuitRenderer::kCarBottom; ++y) {
    for (int x = 150; x < 210; ++x) found += frame[y * kSize + x] == body;
  }
  TEST_ASSERT_GREATER_THAN(50, found);
}

void test_sprites_at_the_edge_are_clipped_without_overrun() {
  CircuitGame game;
  CircuitRenderer renderer;
  CircuitScene scene = sceneOn(game, drehklang::games::kTracks[0], 3000);
  for (int i = 0; i < scene.carCount; ++i) {
    scene.cars[i].x = (i % 2) ? 2400 : -2400;
    scene.cars[i].z = scene.z + 900 + i * 150;
  }
  renderer.prepare(scene);
  constexpr int kRows = 16;
  constexpr int kGuard = 64;
  for (int y0 = 0; y0 < kSize; y0 += kRows) {
    const CircuitRenderer::Span span = CircuitRenderer::stripeSpan(y0, kRows);
    std::vector<uint16_t> buffer(kGuard + span.width * kRows + kGuard, kSentinel);
    renderer.renderStripe(buffer.data() + kGuard, y0, kRows, span);
    for (int i = 0; i < kGuard; ++i) {
      TEST_ASSERT_EQUAL_HEX16(kSentinel, buffer[i]);
      TEST_ASSERT_EQUAL_HEX16(kSentinel, buffer[buffer.size() - 1 - i]);
    }
  }
}

void test_every_phase_and_track_renders() {
  for (int track = 0; track < drehklang::games::kTrackCount; ++track) {
    CircuitGame game;
    CircuitRenderer renderer;
    CircuitScene scene = sceneOn(game, drehklang::games::kTracks[track], 4000);
    scene.trackIndex = track;
    for (auto phase : {CircuitGame::Phase::Select, CircuitGame::Phase::Countdown,
                       CircuitGame::Phase::Race, CircuitGame::Phase::Over}) {
      scene.phase = phase;
      const auto frame = renderFrame(renderer, scene);
      TEST_ASSERT_NOT_EQUAL(kSentinel, frame[180 * kSize + 180]);
    }
  }
}

void test_the_same_scene_draws_the_same_pixels() {
  CircuitGame game;
  CircuitRenderer a;
  CircuitRenderer b;
  const CircuitScene scene = sceneOn(game, drehklang::games::kTracks[1], 5000);
  TEST_ASSERT_TRUE(renderFrame(a, scene) == renderFrame(b, scene));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_spans_follow_the_circle);
  RUN_TEST(test_nothing_is_written_outside_the_circle);
  RUN_TEST(test_everything_inside_the_circle_is_drawn);
  RUN_TEST(test_a_straight_road_is_centred);
  RUN_TEST(test_the_road_narrows_toward_the_horizon);
  RUN_TEST(test_a_right_bend_leads_the_road_right);
  RUN_TEST(test_the_player_car_is_drawn_at_the_bottom_centre);
  RUN_TEST(test_sprites_at_the_edge_are_clipped_without_overrun);
  RUN_TEST(test_every_phase_and_track_renders);
  RUN_TEST(test_the_same_scene_draws_the_same_pixels);
  return UNITY_END();
}
