#pragma once

#include "BtController.h"

namespace drehklang::bluetooth {

enum class ButtonAction : uint8_t { None, TogglePlayer, StartTone, StopTone };

// What the screen is doing when a headphone button arrives.
struct ButtonContext {
  bool inGame = false;
  bool onTones = false;
  bool toneRunning = false;
  bool hasTrack = false;  // Playing, paused, parked or cued.
  bool playing = false;
};

// Play/Pause from the headphones (ADR 0027): the player's own button in the
// music, start/stop on Tones, nothing in a game. Play and Pause are not a
// toggle -- headphones repeat the one they think is due, and a toggle would
// undo it.
inline ButtonAction actionFor(BtEvent event, const ButtonContext &c) {
  if (event != BtEvent::Play && event != BtEvent::Pause) return ButtonAction::None;
  const bool play = event == BtEvent::Play;
  if (c.inGame) return ButtonAction::None;
  if (c.onTones) {
    if (play && !c.toneRunning) return ButtonAction::StartTone;
    if (!play && c.toneRunning) return ButtonAction::StopTone;
    return ButtonAction::None;
  }
  if (!c.hasTrack || play == c.playing) return ButtonAction::None;
  return ButtonAction::TogglePlayer;
}

}  // namespace drehklang::bluetooth
