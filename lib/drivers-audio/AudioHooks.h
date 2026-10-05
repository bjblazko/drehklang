#pragma once

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

}  // namespace drehklang::drivers
