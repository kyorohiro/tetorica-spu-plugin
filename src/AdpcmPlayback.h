// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#pragma once
#include "SpuAdpcm.h"

namespace tetorica {
// Pure non-RT metadata edits. The input is fully validated before copying and
// is never modified. MIDI root is 0..127, equal temperament with A4=440 Hz.
// Sample bytes, existing loop position, volumes, envelope/send, name and
// inspector pitch survive a root edit. Authored waveform metadata is removed
// when its fixed 44100/112 Hz tuning would no longer describe the sample.
RawAdpcmSample withAdpcmRoot(const RawAdpcmSample& sample,int midiNote);
// Normalizes the entire validated sample to one-shot or a loop from frame 0.
// No sample data nibble, tuning or authored envelope is changed. Loop packets
// start with flag 6 (repeat plus marker), end with flag 3 (END plus repeat),
// and use flag 0 between them; a one-packet loop has flag 7. One-shot clears
// repeat/markers and ends with flag 1.
// Incompatible optional waveform metadata is removed, including loop-off.
RawAdpcmSample withAdpcmPlayback(const RawAdpcmSample& sample,bool loopFromStart);
} // namespace tetorica
