#include <unity.h>

#include "HeadphoneButton.h"

using namespace drehklang::bluetooth;

void setUp() {}
void tearDown() {}

void test_pause_while_playing_toggles_the_player() {
  ButtonContext c;
  c.hasTrack = true;
  c.playing = true;
  TEST_ASSERT_EQUAL(static_cast<int>(ButtonAction::TogglePlayer),
                    static_cast<int>(actionFor(BtEvent::Pause, c)));
}

void test_play_while_playing_does_nothing() {
  ButtonContext c;
  c.hasTrack = true;
  c.playing = true;
  TEST_ASSERT_EQUAL(static_cast<int>(ButtonAction::None),
                    static_cast<int>(actionFor(BtEvent::Play, c)));
}

void test_play_while_paused_toggles_the_player() {
  ButtonContext c;
  c.hasTrack = true;
  TEST_ASSERT_EQUAL(static_cast<int>(ButtonAction::TogglePlayer),
                    static_cast<int>(actionFor(BtEvent::Play, c)));
}

void test_nothing_loaded_does_nothing() {
  ButtonContext c;
  TEST_ASSERT_EQUAL(static_cast<int>(ButtonAction::None),
                    static_cast<int>(actionFor(BtEvent::Play, c)));
}

void test_tones_start_and_stop() {
  ButtonContext c;
  c.onTones = true;
  c.hasTrack = true;
  TEST_ASSERT_EQUAL(static_cast<int>(ButtonAction::StartTone),
                    static_cast<int>(actionFor(BtEvent::Play, c)));
  c.toneRunning = true;
  TEST_ASSERT_EQUAL(static_cast<int>(ButtonAction::StopTone),
                    static_cast<int>(actionFor(BtEvent::Pause, c)));
  TEST_ASSERT_EQUAL(static_cast<int>(ButtonAction::None),
                    static_cast<int>(actionFor(BtEvent::Play, c)));
}

void test_games_ignore_the_button() {
  ButtonContext c;
  c.inGame = true;
  c.hasTrack = true;
  TEST_ASSERT_EQUAL(static_cast<int>(ButtonAction::None),
                    static_cast<int>(actionFor(BtEvent::Play, c)));
}

void test_connection_events_are_not_buttons() {
  ButtonContext c;
  c.hasTrack = true;
  TEST_ASSERT_EQUAL(static_cast<int>(ButtonAction::None),
                    static_cast<int>(actionFor(BtEvent::Connected, c)));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_pause_while_playing_toggles_the_player);
  RUN_TEST(test_play_while_playing_does_nothing);
  RUN_TEST(test_play_while_paused_toggles_the_player);
  RUN_TEST(test_nothing_loaded_does_nothing);
  RUN_TEST(test_tones_start_and_stop);
  RUN_TEST(test_games_ignore_the_button);
  RUN_TEST(test_connection_events_are_not_buttons);
  return UNITY_END();
}
