#pragma once

#include <cstdint>

#include "GraphicEqualizer.h"

namespace drehklang::bluetooth {
class AudioTap;
}

namespace drehklang::drivers {

// Keeps the decoder's output silent while true -- while a resume position
// waits to be applied, the decoder is already playing the track's first
// frames. Called by Esp32AudioI2SDriver; read on the decode task.
void setHoldOutput(bool hold);

// Whether the decoder has produced samples since output was last held --
// proof it is past the file's headers. A resume position must wait for
// this: Vorbis cannot decode without the setup header at the start of the
// file, so seeking before the decoder has read it leaves it broken.
bool decoderProducedSinceHold();

// Bluetooth headphones (ADR 0027): where audio_process_i2s copies what it
// plays, or nullptr. Set once in setup(), before anything plays.
void setBluetoothTap(bluetooth::AudioTap *tap);
bluetooth::AudioTap *bluetoothTap();

// The decoder's current sample rate, for the tap. Set by
// Esp32AudioI2SDriver's loop task.
void setDecoderRate(uint32_t rate);
uint32_t decoderRate();

// Settings > Equalizer (ADR 0029): run on the music before the volume and
// before the spectrum's tap, so what is shown is what is heard. The tone
// generator owns the DAC on its own path and stays flat.
signal::GraphicEqualizer &musicEqualizer();

#ifdef DREHKLANG_EQ_DEBUG
// The longest one equalizer chunk took since the last call, in us, and how
// many words it held. For loop() to print -- the decode task never prints.
uint32_t takeWorstEqualizerUs(uint32_t &words);
// The time all chunks took since the last call, in us, and their words.
uint32_t takeTotalEqualizerUs(uint32_t &words);
#endif

}  // namespace drehklang::drivers
